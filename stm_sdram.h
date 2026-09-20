/**
 * @file    stm_sdram.h
 * @brief   STM32H7 FMC SDRAM 初始化、16 位读写和自检接口
 */
#ifndef STM_SDRAM_H
#define STM_SDRAM_H

#include <stddef.h>
#include <stdint.h>
#ifndef STM_SDRAM_HAL_HEADER
#define STM_SDRAM_HAL_HEADER "stm32h7xx_hal.h"
#endif
#include STM_SDRAM_HAL_HEADER

#define STM_SDRAM_VERSION "1.1.0"

#ifdef __cplusplus
extern "C" {
#endif

// 接口返回状态
typedef enum {
    STM_SDRAM_OK = 0,           // 操作成功
    STM_SDRAM_ERROR_ARGUMENT,   // 参数为空、数值无效或未对齐
    STM_SDRAM_ERROR_CONFIG,     // FMC 配置、HAL 状态无效或重复初始化
    STM_SDRAM_ERROR_HAL,        // HAL 命令或刷新设置失败
    STM_SDRAM_ERROR_NOT_READY,  // 实例尚未初始化成功
    STM_SDRAM_ERROR_RANGE,      // 访问范围超出 SDRAM 容量
    STM_SDRAM_ERROR_VERIFY,     // 读回数据与期望值不一致
    STM_SDRAM_ERROR_CONTEXT,    // 在中断上下文或屏蔽中断时初始化
    STM_SDRAM_ERROR_CANCELLED   // 自检被进度回调取消，数据可能已覆盖
} stm_sdram_status_t;

// 实例须零初始化并在使用期间保持有效，初始化后字段由组件维护
typedef struct {
    SDRAM_HandleTypeDef *hal;          // 绑定的 FMC SDRAM 句柄
    uintptr_t base;                    // SDRAM 映射起始地址
    uint32_t size_bytes;               // SDRAM 容量，单位字节
    uint32_t clock_hz;                 // SDRAM 时钟频率，单位 Hz，向下取整
    uint32_t refresh_count;            // FMC 自动刷新计数值
    HAL_StatusTypeDef last_hal_status; // 最近一次命令或刷新设置的 HAL 状态
    uint8_t ready;                     // 非零允许接口访问，不代表自检通过；调用者不得修改
} stm_sdram_t;

// 自检首次数据不匹配的位置与内容
typedef struct {
    uint32_t offset_bytes; // 相对 SDRAM 起始地址的字节偏移
    uint16_t expected;     // 期望读回的 16 位数据
    uint16_t actual;       // 实际读回的 16 位数据
    uint32_t phase;        // 0：数据线；1/2：索引低位及反码；3/4：索引高位及反码
} stm_sdram_test_error_t;

/**
 * @brief 报告自检进度，可喂狗或请求取消；不得重入组件或访问测试区域。
 *
 * @param completed 已完成的读写字节工作量，含重复扫描，起始为 0。
 * @param total 本次总工作量，等于 size_bytes * 8 + 128。
 * @param user 调用者传入的上下文，可为 NULL。
 * @return 0 继续；非零取消，包括最后一次进度通知。
 * @note 在调用线程同步执行；相邻通知间最多读写 4096 字节，不保证固定时间间隔。
 */
typedef int (*stm_sdram_progress_fn)(uint64_t completed, uint64_t total, void *user);

/**
 * @brief 执行上电序列并配置刷新，独占 FMC SDRAM 控制器。
 *
 * @param dev 零初始化的持久实例；ready 非零时不可重复初始化。
 * @param hal 已完成 GPIO、时序配置的句柄；16 位、ReadBurst 关闭、禁止写保护。
 * @param refresh_period_ms 芯片刷新全部行的最大周期，单位 ms，范围 1～1000。
 * @return STM_SDRAM_OK 成功；STM_SDRAM_ERROR_ARGUMENT 参数无效；STM_SDRAM_ERROR_CONFIG 配置或状态无效；
 *         STM_SDRAM_ERROR_HAL 底层操作失败；STM_SDRAM_ERROR_CONTEXT 中断上下文或中断被屏蔽。
 * @note HAL tick 须正常运行；首次调用 HAL 状态须为 READY，同句柄失败重试允许 PRECHARGED。
 */
stm_sdram_status_t sdram_init(stm_sdram_t *dev, SDRAM_HandleTypeDef *hal,
                            uint32_t refresh_period_ms);

/**
 * @brief 停用实例的接口访问，保留诊断信息；不修改 FMC、MPU 或刷新状态。
 *
 * @param dev 持久实例；调用前须停止所有任务、DMA 和直接指针访问。
 * @return STM_SDRAM_OK 成功，重复停用也成功；STM_SDRAM_ERROR_ARGUMENT 实例为空。
 * @note 修复故障后重新调用 sdram_init() 和自检；停用不拦截直接指针访问。
 */
stm_sdram_status_t sdram_disable(stm_sdram_t *dev);

/**
 * @brief 写入 16 位数据，覆盖目标区域；MPU 须为 Normal non-cacheable、不可执行。
 *
 * @param dev 已初始化的实例；所有访问须由调用者串行化。
 * @param offset 相对 SDRAM 起始地址的字节偏移，须为偶数。
 * @param src 内部 RAM 源缓冲区，须按 2 字节对齐且不与 SDRAM 重叠；count=0 可为 NULL。
 * @param count 写入的 uint16_t 元素数量，0 表示不写入。
 * @return STM_SDRAM_OK 成功；STM_SDRAM_ERROR_ARGUMENT 参数无效；
 *         STM_SDRAM_ERROR_NOT_READY 未就绪；STM_SDRAM_ERROR_RANGE 越界。
 */
stm_sdram_status_t sdram_write16(stm_sdram_t *dev, uint32_t offset,
                               const uint16_t *src, size_t count);

/**
 * @brief 读取 16 位数据；MPU 须为 Normal non-cacheable、不可执行。
 *
 * @param dev 已初始化的实例；所有访问须由调用者串行化。
 * @param offset 相对 SDRAM 起始地址的字节偏移，须为偶数。
 * @param dst 内部 RAM 目标缓冲区，须按 2 字节对齐且不与 SDRAM 重叠；count=0 可为 NULL。
 * @param count 读取的 uint16_t 元素数量，0 表示不读取。
 * @return STM_SDRAM_OK 成功；STM_SDRAM_ERROR_ARGUMENT 参数无效；
 *         STM_SDRAM_ERROR_NOT_READY 未就绪；STM_SDRAM_ERROR_RANGE 越界。
 */
stm_sdram_status_t sdram_read16(stm_sdram_t *dev, uint32_t offset,
                              uint16_t *dst, size_t count);

/**
 * @brief 用固定 16 位数值覆盖目标区域；MPU 须为 Normal non-cacheable、不可执行。
 *
 * @param dev 已初始化的实例；所有访问须由调用者串行化。
 * @param offset 相对 SDRAM 起始地址的字节偏移，须为偶数。
 * @param value 每个 16 位元素的填充值。
 * @param count 填充的 uint16_t 元素数量，0 表示不填充。
 * @return STM_SDRAM_OK 成功；STM_SDRAM_ERROR_ARGUMENT 参数无效；
 *         STM_SDRAM_ERROR_NOT_READY 未就绪；STM_SDRAM_ERROR_RANGE 越界。
 */
stm_sdram_status_t sdram_fill16(stm_sdram_t *dev, uint32_t offset,
                              uint16_t value, size_t count);

/**
 * @brief 执行数据线和地址模式自检，覆盖原数据；MPU 须为 Normal non-cacheable、不可执行。
 *
 * @param dev 已初始化的实例；测试期间禁止其他任务或 DMA 访问测试区域。
 * @param offset 相对 SDRAM 起始地址的字节偏移，须为偶数。
 * @param size_bytes 测试长度，单位字节，须为偶数且不小于 2。
 * @param error 可为 NULL；非空时入口清零，仅 STM_SDRAM_ERROR_VERIFY 时记录首次数据不匹配。
 * @return STM_SDRAM_OK 通过；STM_SDRAM_ERROR_VERIFY 数据不匹配；STM_SDRAM_ERROR_ARGUMENT 参数无效；
 *         STM_SDRAM_ERROR_NOT_READY 未就绪；STM_SDRAM_ERROR_RANGE 越界。
 * @note 数据不匹配自动停用实例；通过仅表示指定区域本次测试通过。
 */
stm_sdram_status_t sdram_test(stm_sdram_t *dev, uint32_t offset,
                            uint32_t size_bytes, stm_sdram_test_error_t *error);

/**
 * @brief 带进度和取消回调的破坏性自检，访问约束与 sdram_test() 相同。
 *
 * @param dev 已初始化的实例；测试期间须独占实例及测试区域。
 * @param offset 相对 SDRAM 起始地址的偶数字节偏移。
 * @param size_bytes 偶数字节长度，至少为 2。
 * @param error 可为 NULL；入口清零，仅校验失败时记录首个数据不匹配。
 * @param progress 可为 NULL；首次内存访问前、数据线测试后及扫描期间报告进度。
 * @param user 传给回调的上下文，可为 NULL。
 * @return 与 sdram_test() 相同，另有 STM_SDRAM_ERROR_CANCELLED 表示主动取消。
 * @note 校验失败自动停用；取消和参数错误保留就绪状态，不恢复已覆盖的数据。
 */
stm_sdram_status_t sdram_test_ex(stm_sdram_t *dev, uint32_t offset,
                               uint32_t size_bytes, stm_sdram_test_error_t *error,
                               stm_sdram_progress_fn progress, void *user);

#ifdef __cplusplus
}
#endif
#endif
