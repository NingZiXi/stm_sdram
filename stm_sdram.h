/**
 * @file    stm_sdram.h
 * @brief   STM32H7 FMC SDRAM 初始化、16 位读写和自检接口
 */
#ifndef STM_SDRAM_H
#define STM_SDRAM_H

#include <stddef.h>
#include <stdint.h>
#include "stm_err.h"
#ifndef STM_SDRAM_HAL_HEADER
#define STM_SDRAM_HAL_HEADER "stm32h7xx_hal.h"
#endif
#include STM_SDRAM_HAL_HEADER

#define STM_SDRAM_VERSION "2.0.0"

#ifdef __cplusplus
extern "C" {
#endif

// 由 create 创建、delete 释放的不透明句柄
typedef struct sdram_context *sdram_handle_t;

typedef struct {
    SDRAM_HandleTypeDef *hal; // 已初始化并在句柄使用期间保持有效的 HAL 外设
    uint32_t refresh_period_ms; // 芯片全行刷新周期，1～1000 ms
} sdram_config_t;

// 可通过 get_info 查询的只读快照
typedef struct {
    uintptr_t base;                    // SDRAM 映射起始地址
    uint32_t size_bytes;               // SDRAM 容量，单位字节
    uint32_t clock_hz;                 // SDRAM 时钟频率，单位 Hz，向下取整
    uint32_t refresh_count;            // FMC 自动刷新计数值
    HAL_StatusTypeDef last_hal_status; // 最近一次命令或刷新设置的 HAL 状态
    uint8_t ready;                     // 非零允许接口访问，不代表自检通过
} sdram_info_t;

// 自检首次数据不匹配的位置与内容
typedef struct {
    uint32_t offset_bytes; // 相对 SDRAM 起始地址的字节偏移
    uint16_t expected;     // 期望读回的 16 位数据
    uint16_t actual;       // 实际读回的 16 位数据
    uint32_t phase;        // 0：数据线；1/2：索引低位及反码；3/4：索引高位及反码
} sdram_test_error_t;

/**
 * @brief 创建并初始化设备；失败不返回新对象，不执行破坏性自检。
 * @param[in] config 创建配置，仅在调用期间读取；HAL 句柄由板级持有。
 * @param[out] out_handle 输出位置，调用前必须为 NULL；已有句柄时保持原值并拒绝创建。
 * @return STM_OK 或参数、配置、状态、内存、上下文及设备访问错误。
 */
stm_err_t sdram_create(const sdram_config_t *config, sdram_handle_t *out_handle);

/**
 * @brief 释放组件对象并清空句柄，不关闭 HAL 外设；调用前停止所有相关访问。
 * @param[in,out] handle 句柄地址，*handle 为 NULL 时也成功；其他别名不会自动清空。
 * @return STM_OK、STM_ERR_INVALID_ARG 或 STM_ERR_INVALID_CONTEXT。
 */
stm_err_t sdram_delete(sdram_handle_t *handle);

/**
 * @brief 查询实例信息，故障停用后仍可查询，不访问硬件。
 * @param handle 有效实例。
 * @param[out] info 输出快照。
 * @return STM_OK 或 STM_ERR_INVALID_ARG。
 */
stm_err_t sdram_get_info(sdram_handle_t handle, sdram_info_t *info);

/**
 * @brief 报告自检进度，可喂狗或请求取消；不得重入组件或访问测试区域。
 *
 * @param completed 已完成的读写字节工作量，含重复扫描，起始为 0。
 * @param total 本次总工作量，等于 size_bytes * 8 + 128。
 * @param user 调用者传入的上下文，可为 NULL。
 * @return 0 继续；非零取消，包括最后一次进度通知。
 * @note 在调用线程同步执行；相邻通知间最多读写 4096 字节，不保证固定时间间隔。
 */
typedef int (*sdram_progress_cb_t)(uint64_t completed, uint64_t total, void *user);

/**
 * @brief 写入 16 位数据，覆盖目标区域；MPU 须为 Normal non-cacheable、不可执行。
 *
 * @param dev 已初始化的实例；所有访问须由调用者串行化。
 * @param offset_bytes 相对 SDRAM 起始地址的字节偏移，须为偶数。
 * @param src 内部 RAM 源缓冲区，须按 2 字节对齐且不与 SDRAM 重叠；count=0 可为 NULL。
 * @param count 写入的 uint16_t 元素数量，0 表示不写入。
 * @return STM_OK 成功；STM_ERR_INVALID_ARG 参数无效；
 *         STM_ERR_INVALID_STATE 未就绪；STM_ERR_OUT_OF_RANGE 越界。
 */
stm_err_t sdram_write16(sdram_handle_t dev, uint32_t offset_bytes,
                               const uint16_t *src, size_t count);

/**
 * @brief 读取 16 位数据；MPU 须为 Normal non-cacheable、不可执行。
 *
 * @param dev 已初始化的实例；所有访问须由调用者串行化。
 * @param offset_bytes 相对 SDRAM 起始地址的字节偏移，须为偶数。
 * @param dst 内部 RAM 目标缓冲区，须按 2 字节对齐且不与 SDRAM 重叠；count=0 可为 NULL。
 * @param count 读取的 uint16_t 元素数量，0 表示不读取。
 * @return STM_OK 成功；STM_ERR_INVALID_ARG 参数无效；
 *         STM_ERR_INVALID_STATE 未就绪；STM_ERR_OUT_OF_RANGE 越界。
 */
stm_err_t sdram_read16(sdram_handle_t dev, uint32_t offset_bytes,
                              uint16_t *dst, size_t count);

/**
 * @brief 用固定 16 位数值覆盖目标区域；MPU 须为 Normal non-cacheable、不可执行。
 *
 * @param dev 已初始化的实例；所有访问须由调用者串行化。
 * @param offset_bytes 相对 SDRAM 起始地址的字节偏移，须为偶数。
 * @param value 每个 16 位元素的填充值。
 * @param count 填充的 uint16_t 元素数量，0 表示不填充。
 * @return STM_OK 成功；STM_ERR_INVALID_ARG 参数无效；
 *         STM_ERR_INVALID_STATE 未就绪；STM_ERR_OUT_OF_RANGE 越界。
 */
stm_err_t sdram_fill16(sdram_handle_t dev, uint32_t offset_bytes,
                              uint16_t value, size_t count);

/**
 * @brief 执行数据线和地址模式自检，覆盖原数据；MPU 须为 Normal non-cacheable、不可执行。
 *
 * @param dev 已初始化的实例；测试期间禁止其他任务或 DMA 访问测试区域。
 * @param offset_bytes 相对 SDRAM 起始地址的字节偏移，须为偶数。
 * @param size_bytes 测试长度，单位字节，须为偶数且不小于 2。
 * @param error 可为 NULL；非空时入口清零，仅 STM_ERR_VERIFY 时记录首次数据不匹配。
 * @return STM_OK 通过；STM_ERR_VERIFY 数据不匹配；STM_ERR_INVALID_ARG 参数无效；
 *         STM_ERR_INVALID_STATE 未就绪；STM_ERR_OUT_OF_RANGE 越界。
 * @note 数据不匹配自动停用实例；通过仅表示指定区域本次测试通过。
 */
stm_err_t sdram_test(sdram_handle_t dev, uint32_t offset_bytes,
                            uint32_t size_bytes, sdram_test_error_t *error);

/**
 * @brief 带进度和取消回调的破坏性自检，访问约束与 sdram_test() 相同。
 *
 * @param dev 已初始化的实例；测试期间须独占实例及测试区域。
 * @param offset_bytes 相对 SDRAM 起始地址的偶数字节偏移。
 * @param size_bytes 偶数字节长度，至少为 2。
 * @param error 可为 NULL；入口清零，仅校验失败时记录首个数据不匹配。
 * @param progress 可为 NULL；首次内存访问前、数据线测试后及扫描期间报告进度。
 * @param user 传给回调的上下文，可为 NULL。
 * @return 与 sdram_test() 相同，另有 STM_ERR_CANCELLED 表示主动取消。
 * @note 校验失败自动停用；取消和参数错误保留就绪状态，不恢复已覆盖的数据。
 */
stm_err_t sdram_test_ex(sdram_handle_t dev, uint32_t offset_bytes,
                               uint32_t size_bytes, sdram_test_error_t *error,
                               sdram_progress_cb_t progress, void *user);

#ifdef __cplusplus
}
#endif
#endif
