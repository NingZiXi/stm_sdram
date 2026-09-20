/**
 * @file    stm_sdram.c
 * @brief   STM32H7 FMC SDRAM 上电、刷新、16 位访问和自检实现
 */
#include "stm_sdram.h"

// HAL H7 V1.13.0 忽略命令 Timeout；HAL_Delay 要求 tick 正常运行。
#define SDRAM_COMMAND_TIMEOUT_MS 100U
#define SDRAM_STARTUP_DELAY_MS 1U
#define SDRAM_AUTO_REFRESH_CYCLES 8U

// 发送 SDRAM 命令并等待完成
static stm_sdram_status_t sdram_send_command(stm_sdram_t *dev, uint32_t mode,
                                      uint32_t refreshes, uint32_t mode_register)
{
    FMC_SDRAM_CommandTypeDef command = {0};
    command.CommandMode = mode;
    command.CommandTarget = dev->hal->Init.SDBank == FMC_SDRAM_BANK1
                                ? FMC_SDRAM_CMD_TARGET_BANK1 : FMC_SDRAM_CMD_TARGET_BANK2;
    command.AutoRefreshNumber = refreshes;
    command.ModeRegisterDefinition = mode_register;
    dev->last_hal_status = HAL_SDRAM_SendCommand(dev->hal, &command,
                                                SDRAM_COMMAND_TIMEOUT_MS);
    if (dev->last_hal_status != HAL_OK) {
        return STM_SDRAM_ERROR_HAL;
    }
    // 命令间等待 1 ms，覆盖 tRP、8*tRFC 和 tMRD。
    HAL_Delay(SDRAM_STARTUP_DELAY_MS);
    return STM_SDRAM_OK;
}

// 初始化 SDRAM 并设置自动刷新
stm_sdram_status_t sdram_init(stm_sdram_t *dev, SDRAM_HandleTypeDef *hal,
                                uint32_t refresh_period_ms)
{
    uint32_t rows, columns, banks, cas, divider, kernel_hz;
    uint64_t refresh;
    stm_sdram_status_t status;

    if (dev == NULL || hal == NULL || refresh_period_ms == 0U || refresh_period_ms > 1000U) {
        return STM_SDRAM_ERROR_ARGUMENT;
    }
    if (__get_IPSR() != 0U || __get_PRIMASK() != 0U ||
        __get_BASEPRI() != 0U || __get_FAULTMASK() != 0U) {
        return STM_SDRAM_ERROR_CONTEXT;
    }
    HAL_SDRAM_StateTypeDef hal_state = HAL_SDRAM_GetState(hal);
    // PRECHARGED 状态仅允许同一句柄的未完成实例重试。
    int retry_precharged = hal_state == HAL_SDRAM_STATE_PRECHARGED && dev->hal == hal;
    if (dev->ready != 0U || hal->Instance != FMC_SDRAM_DEVICE ||
        (hal_state != HAL_SDRAM_STATE_READY && !retry_precharged) ||
        hal->Init.MemoryDataWidth != FMC_SDRAM_MEM_BUS_WIDTH_16 ||
        hal->Init.ReadBurst != FMC_SDRAM_RBURST_DISABLE ||
        (hal->Init.SDBank != FMC_SDRAM_BANK1 && hal->Init.SDBank != FMC_SDRAM_BANK2) ||
        hal->Init.WriteProtection != FMC_SDRAM_WRITE_PROTECTION_DISABLE) {
        return STM_SDRAM_ERROR_CONFIG;
    }
    switch (hal->Init.RowBitsNumber) {
    case FMC_SDRAM_ROW_BITS_NUM_11: rows = 11U; break;
    case FMC_SDRAM_ROW_BITS_NUM_12: rows = 12U; break;
    case FMC_SDRAM_ROW_BITS_NUM_13: rows = 13U; break;
    default: return STM_SDRAM_ERROR_CONFIG;
    }
    switch (hal->Init.ColumnBitsNumber) {
    case FMC_SDRAM_COLUMN_BITS_NUM_8: columns = 8U; break;
    case FMC_SDRAM_COLUMN_BITS_NUM_9: columns = 9U; break;
    case FMC_SDRAM_COLUMN_BITS_NUM_10: columns = 10U; break;
    case FMC_SDRAM_COLUMN_BITS_NUM_11: columns = 11U; break;
    default: return STM_SDRAM_ERROR_CONFIG;
    }
    switch (hal->Init.InternalBankNumber) {
    case FMC_SDRAM_INTERN_BANKS_NUM_2: banks = 2U; break;
    case FMC_SDRAM_INTERN_BANKS_NUM_4: banks = 4U; break;
    default: return STM_SDRAM_ERROR_CONFIG;
    }
    switch (hal->Init.CASLatency) {
    case FMC_SDRAM_CAS_LATENCY_2: cas = 2U; break;
    case FMC_SDRAM_CAS_LATENCY_3: cas = 3U; break;
    default: return STM_SDRAM_ERROR_CONFIG;
    }
    switch (hal->Init.SDClockPeriod) {
    case FMC_SDRAM_CLOCK_PERIOD_2: divider = 2U; break;
    case FMC_SDRAM_CLOCK_PERIOD_3: divider = 3U; break;
    default: return STM_SDRAM_ERROR_CONFIG;
    }
    kernel_hz = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_FMC);
    // 刷新间隔向下取整，并预留 20 个 SDCLK。
    refresh = ((uint64_t)kernel_hz * refresh_period_ms) /
              ((uint64_t)divider * 1000U * (1UL << rows));
    if (refresh <= 20U || refresh - 20U > 8191U) {
        return STM_SDRAM_ERROR_CONFIG;
    }
    dev->hal = hal;
    dev->base = hal->Init.SDBank == FMC_SDRAM_BANK1 ? 0xC0000000UL : 0xD0000000UL;
    dev->size_bytes = (1UL << (rows + columns)) * banks * 2U;
    dev->clock_hz = kernel_hz / divider;
    dev->refresh_count = (uint32_t)refresh - 20U;
    dev->last_hal_status = HAL_OK;

    status = sdram_send_command(dev, FMC_SDRAM_CMD_CLK_ENABLE, 1U, 0U);
    if (status != STM_SDRAM_OK) { return status; }
    status = sdram_send_command(dev, FMC_SDRAM_CMD_PALL, 1U, 0U);
    if (status != STM_SDRAM_OK) { return status; }
    status = sdram_send_command(dev, FMC_SDRAM_CMD_AUTOREFRESH_MODE, SDRAM_AUTO_REFRESH_CYCLES, 0U);
    if (status != STM_SDRAM_OK) { return status; }
    // BL=1、顺序突发、CAS 位于 A6:A4、标准模式、单位置写突发。
    status = sdram_send_command(dev, FMC_SDRAM_CMD_LOAD_MODE, 1U, (cas << 4U) | (1UL << 9U));
    if (status != STM_SDRAM_OK) { return status; }
    dev->last_hal_status = HAL_SDRAM_ProgramRefreshRate(hal, dev->refresh_count);
    if (dev->last_hal_status != HAL_OK) { return STM_SDRAM_ERROR_HAL; }
    __DSB();
    dev->ready = 1U;
    return STM_SDRAM_OK;
}

// 停用实例并保留诊断信息
stm_sdram_status_t sdram_disable(stm_sdram_t *dev)
{
    if (dev == NULL) { return STM_SDRAM_ERROR_ARGUMENT; }
    dev->ready = 0U;
    return STM_SDRAM_OK;
}

// 检查实例状态、对齐和访问范围
static stm_sdram_status_t sdram_check_range(const stm_sdram_t *dev, uint32_t offset, size_t count)
{
    if (dev == NULL) { return STM_SDRAM_ERROR_ARGUMENT; }
    if (dev->ready == 0U) { return STM_SDRAM_ERROR_NOT_READY; }
    if ((offset & 1U) != 0U) { return STM_SDRAM_ERROR_ARGUMENT; }
    // 除法检查范围，避免 offset + count * 2 溢出。
    if (offset > dev->size_bytes || count > (dev->size_bytes - offset) / 2U) {
        return STM_SDRAM_ERROR_RANGE;
    }
    return STM_SDRAM_OK;
}

// 检查缓冲区对齐、地址溢出及 SDRAM 重叠
static int sdram_valid_buffer(const stm_sdram_t *dev, const void *buffer, size_t count)
{
    uintptr_t address = (uintptr_t)buffer;
    size_t bytes = count * 2U; // count 已通过 sdram_check_range 校验。
    if (count == 0U) { return 1; }
    if (buffer == NULL || (address & 1U) != 0U || address > UINTPTR_MAX - bytes) { return 0; }
    return address + bytes <= dev->base || address >= dev->base + dev->size_bytes;
}

// 向 SDRAM 写入 16 位数据
stm_sdram_status_t sdram_write16(stm_sdram_t *dev, uint32_t offset,
                                   const uint16_t *src, size_t count)
{
    stm_sdram_status_t status = sdram_check_range(dev, offset, count);
    if (status != STM_SDRAM_OK) { return status; }
    if (!sdram_valid_buffer(dev, src, count)) { return STM_SDRAM_ERROR_ARGUMENT; }
    volatile uint16_t *memory = (volatile uint16_t *)(dev->base + offset);
    for (size_t i = 0; i < count; ++i) { memory[i] = src[i]; }
    __DSB();
    return STM_SDRAM_OK;
}

// 从 SDRAM 读取 16 位数据
stm_sdram_status_t sdram_read16(stm_sdram_t *dev, uint32_t offset,
                                  uint16_t *dst, size_t count)
{
    stm_sdram_status_t status = sdram_check_range(dev, offset, count);
    if (status != STM_SDRAM_OK) { return status; }
    if (!sdram_valid_buffer(dev, dst, count)) { return STM_SDRAM_ERROR_ARGUMENT; }
    const volatile uint16_t *memory = (const volatile uint16_t *)(dev->base + offset);
    for (size_t i = 0; i < count; ++i) { dst[i] = memory[i]; }
    return STM_SDRAM_OK;
}

// 用固定 16 位数值填充 SDRAM
stm_sdram_status_t sdram_fill16(stm_sdram_t *dev, uint32_t offset,
                                  uint16_t value, size_t count)
{
    stm_sdram_status_t status = sdram_check_range(dev, offset, count);
    if (status != STM_SDRAM_OK) { return status; }
    volatile uint16_t *memory = (volatile uint16_t *)(dev->base + offset);
    for (size_t i = 0; i < count; ++i) { memory[i] = value; }
    __DSB();
    return STM_SDRAM_OK;
}

// 记录首个数据不匹配并返回校验错误
static stm_sdram_status_t sdram_mismatch(stm_sdram_t *dev, stm_sdram_test_error_t *error, uint32_t offset,
                                 uint16_t expected, uint16_t actual, uint32_t phase)
{
    if (error != NULL) {
        error->offset_bytes = offset;
        error->expected = expected;
        error->actual = actual;
        error->phase = phase;
    }
    (void)sdram_disable(dev);
    return STM_SDRAM_ERROR_VERIFY;
}

// 生成半字索引的低位、高位或反码测试值
static uint16_t sdram_pattern(uint32_t index, uint32_t phase)
{
    uint16_t value = (uint16_t)((phase < 2U) ? index : (index >> 16U));
    return (phase & 1U) != 0U ? (uint16_t)~value : value;
}

// 执行无回调的破坏性自检
stm_sdram_status_t sdram_test(stm_sdram_t *dev, uint32_t offset,
                            uint32_t size_bytes, stm_sdram_test_error_t *error)
{
    return sdram_test_ex(dev, offset, size_bytes, error, NULL, NULL);
}

// 执行带进度和取消回调的破坏性自检
stm_sdram_status_t sdram_test_ex(stm_sdram_t *dev, uint32_t offset,
                               uint32_t size_bytes, stm_sdram_test_error_t *error,
                               stm_sdram_progress_fn progress, void *user)
{
    if (error != NULL) { *error = (stm_sdram_test_error_t){0}; }
    if ((size_bytes & 1U) != 0U || size_bytes < 2U) { return STM_SDRAM_ERROR_ARGUMENT; }
    stm_sdram_status_t status = sdram_check_range(dev, offset, size_bytes / 2U);
    if (status != STM_SDRAM_OK) { return status; }
    const uint64_t total = (uint64_t)size_bytes * 8U + 128U;
    uint64_t completed = 0U;
    if (progress != NULL && progress(0U, total, user) != 0) {
        return STM_SDRAM_ERROR_CANCELLED;
    }
    volatile uint16_t *memory = (volatile uint16_t *)(dev->base + offset);
    // 起始半字执行 walking 1/0，共 32 次写入和读回。
    for (uint32_t bit = 0U; bit < 16U; ++bit) {
        for (uint32_t inverse = 0U; inverse < 2U; ++inverse) {
            uint16_t expected = (uint16_t)(1U << bit);
            if (inverse != 0U) { expected = (uint16_t)~expected; }
            memory[0] = expected;
            __DSB();
            uint16_t actual = memory[0];
            if (actual != expected) { return sdram_mismatch(dev, error, offset, expected, actual, 0U); }
        }
    }
    completed = 128U;
    if (progress != NULL && progress(completed, total, user) != 0) {
        return STM_SDRAM_ERROR_CANCELLED;
    }
    // 每轮先整区写入再整区校验，保留跨分块地址别名检测。
    for (uint32_t phase = 0U; phase < 4U; ++phase) {
        for (uint32_t i = 0U; i < size_bytes / 2U; ++i) {
            memory[i] = sdram_pattern(offset / 2U + i, phase);
            completed += 2U;
            if (progress != NULL && (((i + 1U) % 2048U) == 0U || i + 1U == size_bytes / 2U)) {
                __DSB();
                if (progress(completed, total, user) != 0) { return STM_SDRAM_ERROR_CANCELLED; }
            }
        }
        __DSB();
        for (uint32_t i = 0U; i < size_bytes / 2U; ++i) {
            uint16_t expected = sdram_pattern(offset / 2U + i, phase);
            uint16_t actual = memory[i];
            if (actual != expected) {
                return sdram_mismatch(dev, error, offset + i * 2U, expected, actual, phase + 1U);
            }
            completed += 2U;
            if (progress != NULL && (((i + 1U) % 2048U) == 0U || i + 1U == size_bytes / 2U)) {
                if (progress(completed, total, user) != 0) { return STM_SDRAM_ERROR_CANCELLED; }
            }
        }
    }
    return STM_SDRAM_OK;
}
