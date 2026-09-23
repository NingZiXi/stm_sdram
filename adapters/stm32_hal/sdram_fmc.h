/** @file sdram_fmc.h @brief STM32 HAL FMC SDR SDRAM 可选适配器。 */
#ifndef SDRAM_FMC_H
#define SDRAM_FMC_H
#include "stm_sdram_controller.h"
#ifndef STM_SDRAM_HAL_HEADER
#error "Define STM_SDRAM_HAL_HEADER for the selected STM32 HAL family"
#endif
#include STM_SDRAM_HAL_HEADER
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct
    {
        SDRAM_HandleTypeDef *hal;
        uint32_t kernel_clock_hz; /* 实际 FMC 内核频率，由板级确认时钟源。 */
        HAL_StatusTypeDef last_hal_status;
    } sdram_fmc_context_t;
    sdram_controller_t sdram_fmc_bind(sdram_fmc_context_t *ctx, SDRAM_HandleTypeDef *hal,
                                      uint32_t kernel_clock_hz);
    extern const sdram_controller_ops_t sdram_fmc_ops;
/* 仅绑定；外设时序由 CubeMX/BSP 初始化。ctx 和句柄必须保持有效且不被重新绑定。
 * 同一 FMC 的两个 Bank 共享配置/刷新资源，因此适配器按整个 FMC 独占。 */
#ifdef __cplusplus
}
#endif
#endif
