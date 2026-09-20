/**
 * @file    sdram_fmc_h7.c
 * @brief   STM32H7 FMC 配置校验、刷新计算和上电命令
 */
#include "sdram_internal.h"

// HAL H7 V1.13.0 忽略命令 Timeout；HAL_Delay 要求 tick 正常运行。
#define SDRAM_COMMAND_TIMEOUT_MS 100U
#define SDRAM_STARTUP_DELAY_MS 1U
// 读取实际 FMC 时序寄存器；Bank2 的 tRC/tRP 来自共享的 SDTR1。
static stm_err_t sdram_validate_timing(SDRAM_HandleTypeDef *hal, const sdram_device_t *device,
                                      uint32_t kernel_hz, uint32_t divider)
{
    uint32_t index = hal->Init.SDBank;
    uint32_t own = hal->Instance->SDTR[index];
    uint32_t shared = hal->Instance->SDTR[0];
    for (unsigned i = 0U; i < 7U; ++i) {
        uint32_t cycles = (((i == 3U || i == 5U ? shared : own) >> (i * 4U)) & 15U) + 1U;
        uint64_t ns_cycles = ((uint64_t)kernel_hz * device->timing_ns[i] +
                              (uint64_t)divider * 1000000000U - 1U) /
                             ((uint64_t)divider * 1000000000U);
        if (cycles < device->timing_cycles[i] || cycles < ns_cycles) { return STM_ERR_INVALID_CONFIG; }
    }
    return STM_OK;
}

// 发送 SDRAM 命令并等待完成
static stm_err_t sdram_send_command(sdram_handle_t dev, uint32_t mode,
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
        return dev->last_hal_status == HAL_TIMEOUT ? STM_ERR_TIMEOUT : STM_ERR_IO;
    }
    // 命令间等待 1 ms，覆盖 tRP、8*tRFC 和 tMRD。
    HAL_Delay(mode == FMC_SDRAM_CMD_CLK_ENABLE ? dev->device.startup_delay_ms : SDRAM_STARTUP_DELAY_MS);
    return STM_OK;
}

// 初始化 SDRAM 并设置自动刷新
stm_err_t sdram_init_device(sdram_handle_t dev, SDRAM_HandleTypeDef *hal,
                                const sdram_device_t *device)
{
    uint32_t rows, columns, banks, cas, divider, kernel_hz;
    uint64_t refresh;
    stm_err_t status;

    if (dev == NULL || hal == NULL || device == NULL || device->refresh_period_ms == 0U || device->refresh_period_ms > 1000U ||
        device->startup_delay_ms == 0U || device->startup_delay_ms > 1000U || device->max_clock_hz == 0U ||
        device->auto_refresh_cycles == 0U || device->auto_refresh_cycles > 16U) {
        return STM_ERR_INVALID_ARG;
    }
    if (__get_IPSR() != 0U || __get_PRIMASK() != 0U ||
        __get_BASEPRI() != 0U || __get_FAULTMASK() != 0U) {
        return STM_ERR_INVALID_CONTEXT;
    }
    HAL_SDRAM_StateTypeDef hal_state = HAL_SDRAM_GetState(hal);
    if (dev->ready != 0U || hal->Instance != FMC_SDRAM_DEVICE ||
        hal_state != HAL_SDRAM_STATE_READY ||
        hal->Init.MemoryDataWidth != FMC_SDRAM_MEM_BUS_WIDTH_16 ||
        hal->Init.ReadBurst != FMC_SDRAM_RBURST_DISABLE ||
        (hal->Init.SDBank != FMC_SDRAM_BANK1 && hal->Init.SDBank != FMC_SDRAM_BANK2) ||
        hal->Init.WriteProtection != FMC_SDRAM_WRITE_PROTECTION_DISABLE) {
        return STM_ERR_INVALID_CONFIG;
    }
    switch (hal->Init.RowBitsNumber) {
    case FMC_SDRAM_ROW_BITS_NUM_11: rows = 11U; break;
    case FMC_SDRAM_ROW_BITS_NUM_12: rows = 12U; break;
    case FMC_SDRAM_ROW_BITS_NUM_13: rows = 13U; break;
    default: return STM_ERR_INVALID_CONFIG;
    }
    switch (hal->Init.ColumnBitsNumber) {
    case FMC_SDRAM_COLUMN_BITS_NUM_8: columns = 8U; break;
    case FMC_SDRAM_COLUMN_BITS_NUM_9: columns = 9U; break;
    case FMC_SDRAM_COLUMN_BITS_NUM_10: columns = 10U; break;
    case FMC_SDRAM_COLUMN_BITS_NUM_11: columns = 11U; break;
    default: return STM_ERR_INVALID_CONFIG;
    }
    switch (hal->Init.InternalBankNumber) {
    case FMC_SDRAM_INTERN_BANKS_NUM_2: banks = 2U; break;
    case FMC_SDRAM_INTERN_BANKS_NUM_4: banks = 4U; break;
    default: return STM_ERR_INVALID_CONFIG;
    }
    switch (hal->Init.CASLatency) {
    case FMC_SDRAM_CAS_LATENCY_2: cas = 2U; break;
    case FMC_SDRAM_CAS_LATENCY_3: cas = 3U; break;
    default: return STM_ERR_INVALID_CONFIG;
    }
    switch (hal->Init.SDClockPeriod) {
    case FMC_SDRAM_CLOCK_PERIOD_2: divider = 2U; break;
    case FMC_SDRAM_CLOCK_PERIOD_3: divider = 3U; break;
    default: return STM_ERR_INVALID_CONFIG;
    }
    // H7 HAL 的通用外设频率查询不支持 FMC；HCLK 来源直接读取总线频率。
    kernel_hz = __HAL_RCC_GET_FMC_SOURCE() == RCC_FMCCLKSOURCE_HCLK
                    ? HAL_RCC_GetHCLKFreq() : 0U;
    if (device->row_bits != rows || device->column_bits != columns || device->internal_banks != banks ||
        device->bus_width_bits != 16U || !(device->cas_mask & (1U << cas)) ||
        kernel_hz == 0U || (uint64_t)kernel_hz > (uint64_t)divider * device->max_clock_hz) {
        return STM_ERR_INVALID_CONFIG;
    }
    for (unsigned i = 0U; i < 7U; ++i) {
        if ((device->timing_ns[i] == 0U && device->timing_cycles[i] == 0U) ||
            device->timing_cycles[i] > 16U || device->timing_ns[i] > 1000000U) {
            return STM_ERR_INVALID_ARG;
        }
    }
    status = sdram_validate_timing(hal, device, kernel_hz, divider);
    if (status != STM_OK) { return status; }
    // 刷新间隔向下取整，并预留 20 个 SDCLK。
    refresh = ((uint64_t)kernel_hz * device->refresh_period_ms) /
              ((uint64_t)divider * 1000U * (1UL << rows));
    if (refresh <= 20U || refresh - 20U > 8191U) {
        return STM_ERR_INVALID_CONFIG;
    }
    dev->device = *device;
    dev->cas_latency = (uint8_t)cas;
    dev->hal = hal;
    dev->base = hal->Init.SDBank == FMC_SDRAM_BANK1 ? 0xC0000000UL : 0xD0000000UL;
    dev->size_bytes = (1UL << (rows + columns)) * banks * 2U;
    dev->clock_hz = kernel_hz / divider;
    dev->refresh_count = (uint32_t)refresh - 20U;
    dev->last_hal_status = HAL_OK;

    status = sdram_send_command(dev, FMC_SDRAM_CMD_CLK_ENABLE, 1U, 0U);
    if (status != STM_OK) { return status; }
    status = sdram_send_command(dev, FMC_SDRAM_CMD_PALL, 1U, 0U);
    if (status != STM_OK) { return status; }
    status = sdram_send_command(dev, FMC_SDRAM_CMD_AUTOREFRESH_MODE, dev->device.auto_refresh_cycles, 0U);
    if (status != STM_OK) { return status; }
    // BL=1、顺序突发、CAS 位于 A6:A4、标准模式、单位置写突发。
    status = sdram_send_command(dev, FMC_SDRAM_CMD_LOAD_MODE, 1U, (cas << 4U) | (1UL << 9U));
    if (status != STM_OK) { return status; }
    dev->last_hal_status = HAL_SDRAM_ProgramRefreshRate(hal, dev->refresh_count);
    if (dev->last_hal_status != HAL_OK) { return dev->last_hal_status == HAL_TIMEOUT ? STM_ERR_TIMEOUT : STM_ERR_IO; }
    __DSB();
    dev->ready = 1U;
    return STM_OK;
}
