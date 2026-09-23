/** @file sdram_init.c @brief SDR SDRAM 几何/时序检查与通用启动步骤。 */
#include "sdram_internal.h"
static stm_err_t send(sdram_handle_t d, sdram_command_t cmd, uint32_t n, uint32_t mode)
{
    stm_err_t e = d->controller.ops->command(d->controller.ctx, cmd, n, mode);
    d->last_error = e;
    if (e != STM_OK)
    {
        return e;
    }
    d->controller.ops->delay_ms(d->controller.ctx,
                                cmd == SDRAM_CMD_CLOCK_ENABLE ? d->device.startup_delay_ms : 1U);
    return STM_OK;
}
stm_err_t sdram_init_device(sdram_handle_t d)
{
    const sdram_device_t *p = &d->device;
    if (!p->refresh_period_ms || p->refresh_period_ms > 1000U || !p->startup_delay_ms ||
        p->startup_delay_ms > 1000U || !p->max_clock_hz || !p->auto_refresh_cycles ||
        p->auto_refresh_cycles > 16U || p->row_bits < 1U || p->row_bits > 16U || p->column_bits < 1U ||
        p->column_bits > 16U || (p->internal_banks != 2U && p->internal_banks != 4U) ||
        (p->bus_width_bits != 16U && p->bus_width_bits != 32U))
    {
        return STM_ERR_INVALID_ARG;
    }
    for (unsigned i = 0U; i < SDRAM_TIMING_COUNT; ++i)
    {
        if ((!p->timing_ns[i] && !p->timing_cycles[i]) || p->timing_ns[i] > 1000000U)
        {
            return STM_ERR_INVALID_ARG;
        }
    }
    sdram_controller_info_t c = {0};
    stm_err_t e = d->controller.ops->get_config(d->controller.ctx, &c);
    if (e != STM_OK)
    {
        return e;
    }
    if (!c.clock_numerator_hz || !c.clock_divider || c.clock_divider > 65535U || c.row_bits != p->row_bits ||
        c.column_bits != p->column_bits || c.internal_banks != p->internal_banks ||
        c.bus_width_bits != p->bus_width_bits || c.cas_latency >= 8U ||
        !(p->cas_mask & (1U << c.cas_latency)) || !c.base || (c.base & 1U) ||
        (uint64_t)c.clock_numerator_hz > (uint64_t)c.clock_divider * p->max_clock_hz)
    {
        return STM_ERR_INVALID_CONFIG;
    }
    uint64_t bytes = (1ULL << (p->row_bits + p->column_bits)) * p->internal_banks * (p->bus_width_bits / 8U);
    if (bytes > UINT32_MAX || bytes > c.mapped_size_bytes || bytes > UINTPTR_MAX - c.base)
    {
        return STM_ERR_INVALID_CONFIG;
    }
    for (unsigned i = 0U; i < SDRAM_TIMING_COUNT; ++i)
    {
        uint64_t denom = (uint64_t)c.clock_divider * 1000000000U;
        uint64_t needed = ((uint64_t)c.clock_numerator_hz * p->timing_ns[i] + denom - 1U) / denom;
        if (c.timing_cycles[i] < needed || c.timing_cycles[i] < p->timing_cycles[i])
        {
            return STM_ERR_INVALID_CONFIG;
        }
    }
    uint64_t interval = ((uint64_t)c.clock_numerator_hz * p->refresh_period_ms) /
                        ((uint64_t)c.clock_divider * 1000U * (1UL << p->row_bits));
    if (!interval || interval > UINT32_MAX || !c.refresh_min_cycles || interval < c.refresh_min_cycles ||
        interval > c.refresh_max_cycles)
    {
        return STM_ERR_INVALID_CONFIG;
    }
    d->base = c.base;
    d->size_bytes = (uint32_t)bytes;
    d->cas_latency = c.cas_latency;
    d->clock_hz = c.clock_numerator_hz / c.clock_divider;
    /* 控制器刷新范围已经检查，接下来才发出启动命令。 */
    e = send(d, SDRAM_CMD_CLOCK_ENABLE, 1U, 0U);
    if (e != STM_OK)
    {
        return e;
    }
    e = send(d, SDRAM_CMD_PRECHARGE_ALL, 1U, 0U);
    if (e != STM_OK)
    {
        return e;
    }
    e = send(d, SDRAM_CMD_AUTO_REFRESH, p->auto_refresh_cycles, 0U);
    if (e != STM_OK)
    {
        return e;
    }
    e = send(d, SDRAM_CMD_LOAD_MODE, 1U, ((uint32_t)c.cas_latency << 4U) | (1UL << 9U));
    if (e != STM_OK)
    {
        return e;
    }
    e = d->controller.ops->set_refresh(d->controller.ctx, (uint32_t)interval, &d->refresh_count);
    d->last_error = e;
    if (e != STM_OK)
    {
        return e;
    }
    sdram_barrier(d);
    d->ready = 1U;
    return STM_OK;
}
