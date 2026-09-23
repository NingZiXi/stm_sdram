/** @file sdram_fmc.c @brief FMC 编码、配置读回、命令与刷新寄存器操作。 */
#include "sdram_fmc.h"
static stm_err_t result(sdram_fmc_context_t *c, HAL_StatusTypeDef s)
{
    c->last_hal_status = s;
    return s == HAL_OK ? STM_OK : (s == HAL_TIMEOUT ? STM_ERR_TIMEOUT : STM_ERR_IO);
}
static const void *identity(void *p)
{
    sdram_fmc_context_t *c = p;
    return c->hal ? c->hal->Instance : NULL;
}
static stm_err_t get_config(void *p, sdram_controller_info_t *info)
{
    sdram_fmc_context_t *c = p;
    SDRAM_HandleTypeDef *hal = c->hal;
    uint32_t rows, columns, banks, width, cas, divider;
    if (!hal || !hal->Instance)
    {
        return STM_ERR_INVALID_ARG;
    }
    if (hal->Instance != FMC_SDRAM_DEVICE || HAL_SDRAM_GetState(hal) != HAL_SDRAM_STATE_READY ||
        hal->Init.ReadBurst != FMC_SDRAM_RBURST_DISABLE ||
        (hal->Init.SDBank != FMC_SDRAM_BANK1 && hal->Init.SDBank != FMC_SDRAM_BANK2) ||
        hal->Init.WriteProtection != FMC_SDRAM_WRITE_PROTECTION_DISABLE)
    {
        return STM_ERR_INVALID_CONFIG;
    }
    switch (hal->Init.RowBitsNumber)
    {
    case FMC_SDRAM_ROW_BITS_NUM_11:
        rows = 11U;
        break;
    case FMC_SDRAM_ROW_BITS_NUM_12:
        rows = 12U;
        break;
    case FMC_SDRAM_ROW_BITS_NUM_13:
        rows = 13U;
        break;
    default:
        return STM_ERR_INVALID_CONFIG;
    }
    switch (hal->Init.ColumnBitsNumber)
    {
    case FMC_SDRAM_COLUMN_BITS_NUM_8:
        columns = 8U;
        break;
    case FMC_SDRAM_COLUMN_BITS_NUM_9:
        columns = 9U;
        break;
    case FMC_SDRAM_COLUMN_BITS_NUM_10:
        columns = 10U;
        break;
    case FMC_SDRAM_COLUMN_BITS_NUM_11:
        columns = 11U;
        break;
    default:
        return STM_ERR_INVALID_CONFIG;
    }
    switch (hal->Init.InternalBankNumber)
    {
    case FMC_SDRAM_INTERN_BANKS_NUM_2:
        banks = 2U;
        break;
    case FMC_SDRAM_INTERN_BANKS_NUM_4:
        banks = 4U;
        break;
    default:
        return STM_ERR_INVALID_CONFIG;
    }
    switch (hal->Init.MemoryDataWidth)
    {
    case FMC_SDRAM_MEM_BUS_WIDTH_16:
        width = 16U;
        break;
    case FMC_SDRAM_MEM_BUS_WIDTH_32:
        width = 32U;
        break;
    default:
        return STM_ERR_INVALID_CONFIG;
    }
    switch (hal->Init.CASLatency)
    {
    case FMC_SDRAM_CAS_LATENCY_2:
        cas = 2U;
        break;
    case FMC_SDRAM_CAS_LATENCY_3:
        cas = 3U;
        break;
    default:
        return STM_ERR_INVALID_CONFIG;
    }
    switch (hal->Init.SDClockPeriod)
    {
    case FMC_SDRAM_CLOCK_PERIOD_2:
        divider = 2U;
        break;
    case FMC_SDRAM_CLOCK_PERIOD_3:
        divider = 3U;
        break;
    default:
        return STM_ERR_INVALID_CONFIG;
    }

    *info = (sdram_controller_info_t){
        .base = hal->Init.SDBank == FMC_SDRAM_BANK1 ? 0xC0000000UL : 0xD0000000UL,
        .mapped_size_bytes = 0x10000000U,
        .refresh_min_cycles = 21U,
        .refresh_max_cycles = 8211U,
        .clock_numerator_hz = c->kernel_clock_hz,
        .clock_divider = divider,
        .row_bits = (uint8_t)rows,
        .column_bits = (uint8_t)columns,
        .internal_banks = (uint8_t)banks,
        .bus_width_bits = (uint8_t)width,
        .cas_latency = (uint8_t)cas,
    };
    uint32_t own = hal->Instance->SDTR[hal->Init.SDBank], shared = hal->Instance->SDTR[0];
    for (unsigned i = 0U; i < SDRAM_TIMING_COUNT; ++i)
    {
        info->timing_cycles[i] = (((i == SDRAM_TRC || i == SDRAM_TRP ? shared : own) >> (i * 4U)) & 15U) + 1U;
    }
    return STM_OK;
}
static stm_err_t command(void *p, sdram_command_t cmd, uint32_t refreshes, uint32_t mode)
{
    sdram_fmc_context_t *c = p;
    uint32_t code;
    switch (cmd)
    {
    case SDRAM_CMD_CLOCK_ENABLE:
        code = FMC_SDRAM_CMD_CLK_ENABLE;
        break;
    case SDRAM_CMD_PRECHARGE_ALL:
        code = FMC_SDRAM_CMD_PALL;
        break;
    case SDRAM_CMD_AUTO_REFRESH:
        code = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
        break;
    case SDRAM_CMD_LOAD_MODE:
        code = FMC_SDRAM_CMD_LOAD_MODE;
        break;
    default:
        return STM_ERR_NOT_SUPPORTED;
    }
    if (refreshes < 1U || refreshes > 16U || mode > 0x1FFFU)
    {
        return STM_ERR_INVALID_ARG;
    }
    FMC_SDRAM_CommandTypeDef command = {.CommandMode = code,
                                        .AutoRefreshNumber = refreshes,
                                        .ModeRegisterDefinition = mode,
                                        .CommandTarget = c->hal->Init.SDBank == FMC_SDRAM_BANK1
                                                             ? FMC_SDRAM_CMD_TARGET_BANK1
                                                             : FMC_SDRAM_CMD_TARGET_BANK2};
    return result(c, HAL_SDRAM_SendCommand(c->hal, &command, 100U));
}
static stm_err_t set_refresh(void *p, uint32_t cycles, uint32_t *programmed)
{
    sdram_fmc_context_t *c = p;
    if (cycles <= 20U || cycles - 20U > 8191U)
    {
        return STM_ERR_INVALID_CONFIG;
    }
    stm_err_t e = result(c, HAL_SDRAM_ProgramRefreshRate(c->hal, cycles - 20U));
    if (e == STM_OK)
    {
        *programmed = cycles - 20U;
    }
    return e;
}
static void delay(void *p, uint32_t ms)
{
    (void)p;
    HAL_Delay(ms);
}
static void barrier(void *p)
{
    (void)p;
    __DSB();
}
static stm_err_t context_ok(void *p)
{
    (void)p;
    return __get_IPSR() == 0U && __get_PRIMASK() == 0U && __get_BASEPRI() == 0U && __get_FAULTMASK() == 0U
               ? STM_OK
               : STM_ERR_INVALID_CONTEXT;
}
const sdram_controller_ops_t sdram_fmc_ops = {.name = "stm32_hal_fmc",
                                              .identity = identity,
                                              .get_config = get_config,
                                              .command = command,
                                              .set_refresh = set_refresh,
                                              .delay_ms = delay,
                                              .barrier = barrier,
                                              .check_context = context_ok};
sdram_controller_t sdram_fmc_bind(sdram_fmc_context_t *c, SDRAM_HandleTypeDef *hal, uint32_t hz)
{
    if (c)
    {
        c->hal = hal;
        c->kernel_clock_hz = hz;
        c->last_hal_status = HAL_OK;
    }
    return (sdram_controller_t){.ops = &sdram_fmc_ops, .ctx = c};
}
