/**
 * @file    sdram_internal.h
 * @brief   SDRAM 通用对象与 FMC 平台后端内部契约
 */
#ifndef SDRAM_INTERNAL_H
#define SDRAM_INTERNAL_H
#include "stm_sdram.h"
struct sdram_context {
    sdram_device_t device;
    uint8_t cas_latency;
    SDRAM_HandleTypeDef *hal;          // 绑定的 FMC SDRAM 句柄
    uintptr_t base;                    // SDRAM 映射起始地址
    uint32_t size_bytes;               // SDRAM 容量，单位字节
    uint32_t clock_hz;                 // SDRAM 时钟频率，单位 Hz，向下取整
    uint32_t refresh_count;            // FMC 自动刷新计数值
    HAL_StatusTypeDef last_hal_status; // 最近一次命令或刷新设置的 HAL 状态
    uint8_t ready;                     // 非零允许接口访问，不代表自检通过；调用者不得修改
    struct sdram_context *next; // 当前组件的实例链表
};

stm_err_t sdram_init_device(sdram_handle_t dev, SDRAM_HandleTypeDef *hal,
                            const sdram_device_t *device);
#endif
