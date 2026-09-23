/** @file test_core.c @brief 原生 SDRAM 控制器模型；无 HAL/CMSIS 和硬件地址。 */
#include "stm_sdram.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x)                                                                                             \
    do                                                                                                       \
    {                                                                                                        \
        if (!(x))                                                                                            \
        {                                                                                                    \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                                  \
            return 1;                                                                                        \
        }                                                                                                    \
    } while (0)
typedef struct
{
    uint16_t memory[512];
    sdram_controller_info_t config;
    sdram_command_t commands[4];
    uint32_t calls, refresh, barriers, delays, mode;
    int blocked;
} model_t;
static const void *identity(void *p)
{
    return p;
}
static stm_err_t get_config(void *p, sdram_controller_info_t *out)
{
    *out = ((model_t *)p)->config;
    return STM_OK;
}
static stm_err_t command(void *p, sdram_command_t c, uint32_t n, uint32_t mode)
{
    model_t *m = p;
    if (n < 1U || m->calls >= 4U)
    {
        return STM_ERR_INVALID_ARG;
    }
    m->commands[m->calls++] = c;
    m->mode = mode;
    return STM_OK;
}
static stm_err_t refresh(void *p, uint32_t cycles, uint32_t *count)
{
    model_t *m = p;
    m->refresh = cycles;
    *count = cycles;
    return STM_OK;
}
static void delay(void *p, uint32_t ms)
{
    ((model_t *)p)->delays += ms;
}
static void barrier(void *p)
{
    ++((model_t *)p)->barriers;
}
static stm_err_t context(void *p)
{
    return ((model_t *)p)->blocked ? STM_ERR_INVALID_CONTEXT : STM_OK;
}
static const sdram_controller_ops_t ops = {.name = "native-test",
                                           .identity = identity,
                                           .get_config = get_config,
                                           .command = command,
                                           .set_refresh = refresh,
                                           .delay_ms = delay,
                                           .barrier = barrier,
                                           .check_context = context};
static void fixture(model_t *m)
{
    memset(m, 0, sizeof(*m));
    m->config = (sdram_controller_info_t){.base = (uintptr_t)m->memory,
                                          .mapped_size_bytes = sizeof(m->memory),
                                          .refresh_min_cycles = 1U,
                                          .refresh_max_cycles = 100000U,
                                          .clock_numerator_hz = 1000000U,
                                          .clock_divider = 1U,
                                          .row_bits = 4U,
                                          .column_bits = 4U,
                                          .internal_banks = 2U,
                                          .bus_width_bits = 16U,
                                          .cas_latency = 3U};
    for (unsigned i = 0; i < SDRAM_TIMING_COUNT; ++i)
    {
        m->config.timing_cycles[i] = 2U;
    }
}
static int cancel(uint64_t done, uint64_t total, void *p)
{
    (void)total;
    (void)p;
    return done > 0;
}
int main(void)
{
    model_t one, two;
    fixture(&one);
    fixture(&two);
    sdram_device_t profile = {.name = "test-geometry",
                              .refresh_period_ms = 64U,
                              .startup_delay_ms = 1U,
                              .max_clock_hz = 1000000U,
                              .row_bits = 4U,
                              .column_bits = 4U,
                              .internal_banks = 2U,
                              .bus_width_bits = 16U,
                              .cas_mask = 1U << 3U,
                              .auto_refresh_cycles = 8U};
    for (unsigned i = 0; i < SDRAM_TIMING_COUNT; ++i)
    {
        profile.timing_ns[i] = 1U;
        profile.timing_cycles[i] = 2U;
    }
    sdram_config_t config = {.device = &profile, .controller = {.ops = &ops, .ctx = &one}};
    sdram_handle_t a = NULL, b = NULL, duplicate = NULL;
    CHECK(sdram_create(&config, &a) == STM_OK && one.calls == 4U && one.mode == 0x230U &&
          one.refresh == 4000U);
    CHECK(one.commands[0] == SDRAM_CMD_CLOCK_ENABLE && one.commands[1] == SDRAM_CMD_PRECHARGE_ALL &&
          one.commands[2] == SDRAM_CMD_AUTO_REFRESH && one.commands[3] == SDRAM_CMD_LOAD_MODE);
    CHECK(sdram_create(&config, &duplicate) == STM_ERR_INVALID_STATE && !duplicate);
    config.controller.ctx = &two;
    CHECK(sdram_create(&config, &b) == STM_OK && b != a);
    profile.row_bits = 1U; /* create 已复制器件约束。 */
    sdram_info_t info;
    CHECK(sdram_get_info(a, &info) == STM_OK && info.row_bits == 4U && info.size_bytes == 1024U);
    uint16_t tx[4] = {0x1234, 0x5678, 0xABCD, 0xEF00}, rx[4];
    CHECK(sdram_write16(a, 1016U, tx, 4U) == STM_OK);
    CHECK(sdram_read16(a, 1016U, rx, 4U) == STM_OK && !memcmp(tx, rx, sizeof(tx)));
    CHECK(two.memory[508] == 0U && one.barriers > 1U);
    CHECK(sdram_read16(a, 1022U, rx, 2U) == STM_ERR_OUT_OF_RANGE);
    CHECK(sdram_write16(a, 0U, one.memory, 4U) == STM_ERR_INVALID_ARG);
    CHECK(sdram_test(a, 0U, 1024U, NULL) == STM_OK);
    CHECK(sdram_test_ex(b, 0U, 1024U, NULL, cancel, NULL) == STM_ERR_CANCELLED);
    one.blocked = 1;
    CHECK(sdram_delete(&a) == STM_ERR_INVALID_CONTEXT && a);
    one.blocked = 0;
    CHECK(sdram_delete(&a) == STM_OK && sdram_delete(&b) == STM_OK);
    profile.row_bits = 4U;
    config.controller.ctx = &one;
    fixture(&one);
    one.config.refresh_max_cycles = 3999U;
    CHECK(sdram_create(&config, &a) == STM_ERR_INVALID_CONFIG && one.calls == 0U && !a);
    fixture(&one);
    one.config.timing_cycles[SDRAM_TRP] = 1U;
    CHECK(sdram_create(&config, &a) == STM_ERR_INVALID_CONFIG && one.calls == 0U);
    fixture(&one);
    one.config.mapped_size_bytes = 512U;
    CHECK(sdram_create(&config, &a) == STM_ERR_INVALID_CONFIG && one.calls == 0U);
    fixture(&one);
    one.config.clock_divider = 0U;
    CHECK(sdram_create(&config, &a) == STM_ERR_INVALID_CONFIG && one.calls == 0U);
    fixture(&one);
    config.controller.ops = NULL;
    CHECK(sdram_create(&config, &a) == STM_ERR_INVALID_CONFIG && one.calls == 0U);
    puts("PASS: no-HAL controller, initialization order, timing/refresh preflight, descriptor snapshot, "
         "independent instances, bounds and self-test");
    return 0;
}
