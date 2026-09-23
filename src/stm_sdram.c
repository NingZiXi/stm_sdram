/**
 * @file    stm_sdram.c
 * @brief   SDRAM 通用生命周期、16 位访问和自检实现
 */
#include "sdram_internal.h"
#include <stdlib.h>

static sdram_handle_t g_devices = NULL;

// 检查实例状态、对齐和访问范围
stm_err_t sdram_check_range(sdram_handle_t dev, uint32_t offset_bytes, size_t count)
{
    if (dev == NULL)
    {
        return STM_ERR_INVALID_ARG;
    }
    if (dev->ready == 0U)
    {
        return STM_ERR_INVALID_STATE;
    }
    if ((offset_bytes & 1U) != 0U)
    {
        return STM_ERR_INVALID_ARG;
    }
    // 除法检查范围，避免 offset_bytes + count * 2 溢出。
    if (offset_bytes > dev->size_bytes || count > (dev->size_bytes - offset_bytes) / 2U)
    {
        return STM_ERR_OUT_OF_RANGE;
    }
    return STM_OK;
}

// 检查缓冲区对齐、地址溢出及 SDRAM 重叠
static int sdram_valid_buffer(sdram_handle_t dev, const void *buffer, size_t count)
{
    uintptr_t address = (uintptr_t)buffer;
    size_t bytes = count * 2U; // count 已通过 sdram_check_range 校验。
    if (count == 0U)
    {
        return 1;
    }
    if (buffer == NULL || (address & 1U) != 0U || address > UINTPTR_MAX - bytes)
    {
        return 0;
    }
    return address + bytes <= dev->base || address >= dev->base + dev->size_bytes;
}

// 向 SDRAM 写入 16 位数据
stm_err_t sdram_write16(sdram_handle_t dev, uint32_t offset_bytes, const uint16_t *src, size_t count)
{
    stm_err_t status = sdram_check_range(dev, offset_bytes, count);
    if (status != STM_OK)
    {
        return status;
    }
    if (!sdram_valid_buffer(dev, src, count))
    {
        return STM_ERR_INVALID_ARG;
    }
    volatile uint16_t *memory = (volatile uint16_t *)(dev->base + offset_bytes);
    for (size_t i = 0; i < count; ++i)
    {
        memory[i] = src[i];
    }
    sdram_barrier(dev);
    return STM_OK;
}

// 从 SDRAM 读取 16 位数据
stm_err_t sdram_read16(sdram_handle_t dev, uint32_t offset_bytes, uint16_t *dst, size_t count)
{
    stm_err_t status = sdram_check_range(dev, offset_bytes, count);
    if (status != STM_OK)
    {
        return status;
    }
    if (!sdram_valid_buffer(dev, dst, count))
    {
        return STM_ERR_INVALID_ARG;
    }
    const volatile uint16_t *memory = (const volatile uint16_t *)(dev->base + offset_bytes);
    for (size_t i = 0; i < count; ++i)
    {
        dst[i] = memory[i];
    }
    return STM_OK;
}

// 用固定 16 位数值填充 SDRAM
stm_err_t sdram_fill16(sdram_handle_t dev, uint32_t offset_bytes, uint16_t value, size_t count)
{
    stm_err_t status = sdram_check_range(dev, offset_bytes, count);
    if (status != STM_OK)
    {
        return status;
    }
    volatile uint16_t *memory = (volatile uint16_t *)(dev->base + offset_bytes);
    for (size_t i = 0; i < count; ++i)
    {
        memory[i] = value;
    }
    sdram_barrier(dev);
    return STM_OK;
}

stm_err_t sdram_create(const sdram_config_t *c, sdram_handle_t *out)
{
    if (!c || !out || !c->device || !c->controller.ctx)
    {
        return STM_ERR_INVALID_ARG;
    }
    if (*out)
    {
        return STM_ERR_INVALID_STATE;
    }
    const sdram_controller_ops_t *o = c->controller.ops;
    if (!o || !o->name || !o->identity || !o->get_config || !o->command || !o->set_refresh || !o->delay_ms ||
        !o->barrier || !o->check_context)
    {
        return STM_ERR_INVALID_CONFIG;
    }
    stm_err_t e = o->check_context(c->controller.ctx);
    if (e != STM_OK)
    {
        return e;
    }
    const void *key = o->identity(c->controller.ctx);
    if (!key)
    {
        return STM_ERR_INVALID_CONFIG;
    }
    for (sdram_handle_t it = g_devices; it; it = it->next)
    {
        if (it->identity == key)
        {
            return STM_ERR_INVALID_STATE;
        }
    }
    sdram_handle_t d = calloc(1U, sizeof(*d));
    if (!d)
    {
        return STM_ERR_NO_MEM;
    }
    d->device = *c->device;
    d->controller = c->controller;
    d->identity = key;
    e = sdram_init_device(d);
    if (e != STM_OK)
    {
        free(d);
        return e;
    }
    d->next = g_devices;
    g_devices = d;
    *out = d;
    return STM_OK;
}
stm_err_t sdram_delete(sdram_handle_t *handle)
{
    if (!handle)
    {
        return STM_ERR_INVALID_ARG;
    }
    if (!*handle)
    {
        return STM_OK;
    }
    sdram_handle_t *link = &g_devices;
    while (*link && *link != *handle)
    {
        link = &(*link)->next;
    }
    if (!*link)
    {
        return STM_ERR_INVALID_ARG;
    }
    sdram_handle_t d = *link;
    stm_err_t e = d->controller.ops->check_context(d->controller.ctx);
    if (e != STM_OK)
    {
        return e;
    }
    *link = d->next;
    free(d);
    *handle = NULL;
    return STM_OK;
}
// 复制信息快照，不暴露可写的内部对象
stm_err_t sdram_get_info(sdram_handle_t handle, sdram_info_t *info)
{
    if (handle == NULL || info == NULL)
    {
        return STM_ERR_INVALID_ARG;
    }
    info->row_bits = handle->device.row_bits;
    info->column_bits = handle->device.column_bits;
    info->internal_banks = handle->device.internal_banks;
    info->bus_width_bits = handle->device.bus_width_bits;
    info->cas_latency = handle->cas_latency;
    info->base = handle->base;
    info->size_bytes = handle->size_bytes;
    info->clock_hz = handle->clock_hz;
    info->refresh_count = handle->refresh_count;
    info->last_error = handle->last_error;
    info->ready = handle->ready;
    return STM_OK;
}
