/** @file sdram_internal.h @brief 私有实例状态与通用上电流程。 */
#ifndef SDRAM_INTERNAL_H
#define SDRAM_INTERNAL_H
#include "stm_sdram.h"
struct sdram_context
{
    sdram_device_t device;
    sdram_controller_t controller;
    const void *identity;
    uintptr_t base;
    uint32_t size_bytes, clock_hz, refresh_count;
    stm_err_t last_error;
    uint8_t cas_latency, ready;
    struct sdram_context *next;
};
stm_err_t sdram_init_device(sdram_handle_t dev);
stm_err_t sdram_check_range(sdram_handle_t dev, uint32_t offset_bytes, size_t count);
static inline void sdram_barrier(sdram_handle_t dev)
{
    dev->controller.ops->barrier(dev->controller.ctx);
}
#endif
