/** @file stm_sdram_controller.h @brief SDRAM 控制器接口，不依赖 HAL。 */
#ifndef STM_SDRAM_CONTROLLER_H
#define STM_SDRAM_CONTROLLER_H
#include <stddef.h>
#include <stdint.h>
#include "stm_err.h"
#include "stm_sdram_device.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef enum
    {
        SDRAM_CMD_CLOCK_ENABLE,
        SDRAM_CMD_PRECHARGE_ALL,
        SDRAM_CMD_AUTO_REFRESH,
        SDRAM_CMD_LOAD_MODE
    } sdram_command_t;
    typedef struct
    {
        uintptr_t base;
        uint32_t mapped_size_bytes;
        uint32_t refresh_min_cycles, refresh_max_cycles; /* 已包含控制器余量的可接受输入范围。 */
        uint32_t clock_numerator_hz, clock_divider;      /* SDCLK 的精确有理数表示。 */
        uint32_t timing_cycles[SDRAM_TIMING_COUNT];      /* 读回已生效配置，不重新配置共享控制器。 */
        uint8_t row_bits, column_bits, internal_banks, bus_width_bits, cas_latency;
    } sdram_controller_info_t;
    typedef struct
    {
        const char *name;
        const void *(*identity)(void *ctx); /* 相互影响的共享资源应返回同一标识。 */
        stm_err_t (*get_config)(void *ctx, sdram_controller_info_t *info);
        stm_err_t (*command)(void *ctx, sdram_command_t command, uint32_t refreshes, uint32_t mode);
        stm_err_t (*set_refresh)(void *ctx, uint32_t interval_cycles, uint32_t *programmed_count);
        void (*delay_ms)(void *ctx, uint32_t ms);
        void (*barrier)(void *ctx); /* 完成此前对非缓存映射的写入。 */
        stm_err_t (*check_context)(void *ctx);
    } sdram_controller_ops_t;
    typedef struct
    {
        const sdram_controller_ops_t *ops;
        void *ctx;
    } sdram_controller_t;
/* ops/ctx 与底层资源须保持有效，实例存活期间不能重新绑定；不提供并发锁。
 * set_refresh 的输入为最大允许间隔；控制器余量/寄存器编码由适配器处理。 */
#ifdef __cplusplus
}
#endif
#endif
