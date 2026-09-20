/**
 * @file    stm_sdram.c
 * @brief   SDRAM 通用生命周期、16 位访问和自检实现
 */
#include "private/sdram_internal.h"
#include <stdlib.h>

static sdram_handle_t g_devices = NULL;


// 检查实例状态、对齐和访问范围
static stm_err_t sdram_check_range(sdram_handle_t dev, uint32_t offset_bytes, size_t count)
{
    if (dev == NULL) { return STM_ERR_INVALID_ARG; }
    if (dev->ready == 0U) { return STM_ERR_INVALID_STATE; }
    if ((offset_bytes & 1U) != 0U) { return STM_ERR_INVALID_ARG; }
    // 除法检查范围，避免 offset_bytes + count * 2 溢出。
    if (offset_bytes > dev->size_bytes || count > (dev->size_bytes - offset_bytes) / 2U) {
        return STM_ERR_OUT_OF_RANGE;
    }
    return STM_OK;
}

// 检查缓冲区对齐、地址溢出及 SDRAM 重叠
static int sdram_valid_buffer(sdram_handle_t dev, const void *buffer, size_t count)
{
    uintptr_t address = (uintptr_t)buffer;
    size_t bytes = count * 2U; // count 已通过 sdram_check_range 校验。
    if (count == 0U) { return 1; }
    if (buffer == NULL || (address & 1U) != 0U || address > UINTPTR_MAX - bytes) { return 0; }
    return address + bytes <= dev->base || address >= dev->base + dev->size_bytes;
}

// 向 SDRAM 写入 16 位数据
stm_err_t sdram_write16(sdram_handle_t dev, uint32_t offset_bytes,
                                   const uint16_t *src, size_t count)
{
    stm_err_t status = sdram_check_range(dev, offset_bytes, count);
    if (status != STM_OK) { return status; }
    if (!sdram_valid_buffer(dev, src, count)) { return STM_ERR_INVALID_ARG; }
    volatile uint16_t *memory = (volatile uint16_t *)(dev->base + offset_bytes);
    for (size_t i = 0; i < count; ++i) { memory[i] = src[i]; }
    __DSB();
    return STM_OK;
}

// 从 SDRAM 读取 16 位数据
stm_err_t sdram_read16(sdram_handle_t dev, uint32_t offset_bytes,
                                  uint16_t *dst, size_t count)
{
    stm_err_t status = sdram_check_range(dev, offset_bytes, count);
    if (status != STM_OK) { return status; }
    if (!sdram_valid_buffer(dev, dst, count)) { return STM_ERR_INVALID_ARG; }
    const volatile uint16_t *memory = (const volatile uint16_t *)(dev->base + offset_bytes);
    for (size_t i = 0; i < count; ++i) { dst[i] = memory[i]; }
    return STM_OK;
}

// 用固定 16 位数值填充 SDRAM
stm_err_t sdram_fill16(sdram_handle_t dev, uint32_t offset_bytes,
                                  uint16_t value, size_t count)
{
    stm_err_t status = sdram_check_range(dev, offset_bytes, count);
    if (status != STM_OK) { return status; }
    volatile uint16_t *memory = (volatile uint16_t *)(dev->base + offset_bytes);
    for (size_t i = 0; i < count; ++i) { memory[i] = value; }
    __DSB();
    return STM_OK;
}

// 记录首个数据不匹配并返回校验错误
static stm_err_t sdram_mismatch(sdram_handle_t dev, sdram_test_error_t *error, uint32_t offset_bytes,
                                 uint16_t expected, uint16_t actual, uint32_t phase)
{
    if (error != NULL) {
        error->offset_bytes = offset_bytes;
        error->expected = expected;
        error->actual = actual;
        error->phase = phase;
    }
    dev->ready = 0U;
    return STM_ERR_VERIFY;
}

// 生成半字索引的低位、高位或反码测试值
static uint16_t sdram_pattern(uint32_t index, uint32_t phase)
{
    uint16_t value = (uint16_t)((phase < 2U) ? index : (index >> 16U));
    return (phase & 1U) != 0U ? (uint16_t)~value : value;
}

// 执行无回调的破坏性自检
stm_err_t sdram_test(sdram_handle_t dev, uint32_t offset_bytes,
                            uint32_t size_bytes, sdram_test_error_t *error)
{
    return sdram_test_ex(dev, offset_bytes, size_bytes, error, NULL, NULL);
}

// 执行带进度和取消回调的破坏性自检
stm_err_t sdram_test_ex(sdram_handle_t dev, uint32_t offset_bytes,
                               uint32_t size_bytes, sdram_test_error_t *error,
                               sdram_progress_cb_t progress, void *user)
{
    if (error != NULL) { *error = (sdram_test_error_t){0}; }
    if ((size_bytes & 1U) != 0U || size_bytes < 2U) { return STM_ERR_INVALID_ARG; }
    stm_err_t status = sdram_check_range(dev, offset_bytes, size_bytes / 2U);
    if (status != STM_OK) { return status; }
    const uint64_t total = (uint64_t)size_bytes * 8U + 128U;
    uint64_t completed = 0U;
    if (progress != NULL && progress(0U, total, user) != 0) {
        return STM_ERR_CANCELLED;
    }
    volatile uint16_t *memory = (volatile uint16_t *)(dev->base + offset_bytes);
    // 起始半字执行 walking 1/0，共 32 次写入和读回。
    for (uint32_t bit = 0U; bit < 16U; ++bit) {
        for (uint32_t inverse = 0U; inverse < 2U; ++inverse) {
            uint16_t expected = (uint16_t)(1U << bit);
            if (inverse != 0U) { expected = (uint16_t)~expected; }
            memory[0] = expected;
            __DSB();
            uint16_t actual = memory[0];
            if (actual != expected) { return sdram_mismatch(dev, error, offset_bytes, expected, actual, 0U); }
        }
    }
    completed = 128U;
    if (progress != NULL && progress(completed, total, user) != 0) {
        return STM_ERR_CANCELLED;
    }
    // 每轮先整区写入再整区校验，保留跨分块地址别名检测。
    for (uint32_t phase = 0U; phase < 4U; ++phase) {
        for (uint32_t i = 0U; i < size_bytes / 2U; ++i) {
            memory[i] = sdram_pattern(offset_bytes / 2U + i, phase);
            completed += 2U;
            if (progress != NULL && (((i + 1U) % 2048U) == 0U || i + 1U == size_bytes / 2U)) {
                __DSB();
                if (progress(completed, total, user) != 0) { return STM_ERR_CANCELLED; }
            }
        }
        __DSB();
        for (uint32_t i = 0U; i < size_bytes / 2U; ++i) {
            uint16_t expected = sdram_pattern(offset_bytes / 2U + i, phase);
            uint16_t actual = memory[i];
            if (actual != expected) {
                return sdram_mismatch(dev, error, offset_bytes + i * 2U, expected, actual, phase + 1U);
            }
            completed += 2U;
            if (progress != NULL && (((i + 1U) % 2048U) == 0U || i + 1U == size_bytes / 2U)) {
                if (progress(completed, total, user) != 0) { return STM_ERR_CANCELLED; }
            }
        }
    }
    return STM_OK;
}

// 创建对象并独占对应外设
stm_err_t sdram_create(const sdram_config_t *config, sdram_handle_t *out_handle)
{
    if (config == NULL || out_handle == NULL || config->hal == NULL || config->device == NULL) { return STM_ERR_INVALID_ARG; }
    if (*out_handle != NULL) { return STM_ERR_INVALID_STATE; }
    if (__get_IPSR() != 0U || __get_PRIMASK() != 0U ||
        __get_BASEPRI() != 0U || __get_FAULTMASK() != 0U) { return STM_ERR_INVALID_CONTEXT; }
    if (g_devices != NULL) { return STM_ERR_INVALID_STATE; }
    sdram_handle_t dev = calloc(1U, sizeof(*dev));
    if (dev == NULL) { return STM_ERR_NO_MEM; }
    stm_err_t err = sdram_init_device(dev, config->hal, config->device);
    if (err != STM_OK) { free(dev); return err; }
    dev->next = g_devices;
    g_devices = dev;
    *out_handle = dev;
    return STM_OK;
}

// 释放软件对象，HAL 外设保持不变
stm_err_t sdram_delete(sdram_handle_t *handle)
{
    if (handle == NULL) { return STM_ERR_INVALID_ARG; }
    if (*handle == NULL) { return STM_OK; }
    if (__get_IPSR() != 0U || __get_PRIMASK() != 0U ||
        __get_BASEPRI() != 0U || __get_FAULTMASK() != 0U) { return STM_ERR_INVALID_CONTEXT; }
    sdram_handle_t *link = &g_devices;
    while (*link != NULL && *link != *handle) { link = &(*link)->next; }
    if (*link == NULL) { return STM_ERR_INVALID_ARG; }
    sdram_handle_t dev = *handle;
    *link = dev->next;
    free(dev);
    *handle = NULL;
    return STM_OK;
}

// 复制信息快照，不暴露可写的内部对象
stm_err_t sdram_get_info(sdram_handle_t handle, sdram_info_t *info)
{
    if (handle == NULL || info == NULL) { return STM_ERR_INVALID_ARG; }
    info->row_bits = handle->device.row_bits;
    info->column_bits = handle->device.column_bits;
    info->internal_banks = handle->device.internal_banks;
    info->bus_width_bits = handle->device.bus_width_bits;
    info->cas_latency = handle->cas_latency;
    info->base = handle->base;
    info->size_bytes = handle->size_bytes;
    info->clock_hz = handle->clock_hz;
    info->refresh_count = handle->refresh_count;
    info->last_hal_status = handle->last_hal_status;
    info->ready = handle->ready;
    return STM_OK;
}
