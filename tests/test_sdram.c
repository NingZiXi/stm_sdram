/**
 * @file    test_sdram.c
 * @brief   使用 HAL 桩验证 SDRAM 驱动及故障处理
 */
#include "stm_sdram.h"

#define CHECK(expr) do { if (!(expr)) { return __LINE__; } } while (0)

static uint32_t calls, delays, refresh_value, fail_call, kernel_clock;
static FMC_SDRAM_CommandTypeDef commands[4];
static HAL_StatusTypeDef injected_status;

// 填充测试运行时内存
void *memset(void *dst, int value, size_t size)
{
    unsigned char *out = dst;
    while (size-- != 0U) { *out++ = (unsigned char)value; }
    return dst;
}

// 返回模拟 FMC 内核时钟
uint32_t HAL_RCCEx_GetPeriphCLKFreq(uint64_t clock)
{
    (void)clock;
    return kernel_clock;
}
// 返回模拟 HAL 状态
HAL_SDRAM_StateTypeDef HAL_SDRAM_GetState(const SDRAM_HandleTypeDef *hal)
{
    return hal->State;
}
// 累计模拟延时
void HAL_Delay(uint32_t ms) { delays += ms; }
// 记录命令并注入 HAL 故障
HAL_StatusTypeDef HAL_SDRAM_SendCommand(SDRAM_HandleTypeDef *hal,
                                      FMC_SDRAM_CommandTypeDef *command, uint32_t timeout)
{
    (void)timeout;
    if (calls < 4U) { commands[calls] = *command; }
    if (++calls == fail_call) { return injected_status; }
    hal->State = command->CommandMode == FMC_SDRAM_CMD_PALL
                     ? HAL_SDRAM_STATE_PRECHARGED : HAL_SDRAM_STATE_READY;
    return HAL_OK;
}
// 记录刷新计数并注入 HAL 故障
HAL_StatusTypeDef HAL_SDRAM_ProgramRefreshRate(SDRAM_HandleTypeDef *hal, uint32_t count)
{
    (void)hal;
    refresh_value = count;
    return ++calls == fail_call ? injected_status : HAL_OK;
}

// 重置 HAL 桩并构造默认 FMC 配置
static SDRAM_HandleTypeDef fixture(void)
{
    SDRAM_HandleTypeDef h = {0};
    calls = delays = refresh_value = fail_call = 0U;
    kernel_clock = 275000000U;
    injected_status = HAL_ERROR;
    h.Instance = FMC_SDRAM_DEVICE;
    h.State = HAL_SDRAM_STATE_READY;
    h.Init.SDBank = FMC_SDRAM_BANK2;
    h.Init.ColumnBitsNumber = FMC_SDRAM_COLUMN_BITS_NUM_9;
    h.Init.RowBitsNumber = FMC_SDRAM_ROW_BITS_NUM_13;
    h.Init.MemoryDataWidth = FMC_SDRAM_MEM_BUS_WIDTH_16;
    h.Init.InternalBankNumber = FMC_SDRAM_INTERN_BANKS_NUM_4;
    h.Init.CASLatency = FMC_SDRAM_CAS_LATENCY_3;
    h.Init.SDClockPeriod = FMC_SDRAM_CLOCK_PERIOD_3;
    h.Init.ReadBurst = FMC_SDRAM_RBURST_DISABLE;
    h.Init.WriteProtection = FMC_SDRAM_WRITE_PROTECTION_DISABLE;
    return h;
}

// 验证初始化、边界检查和读写自检
int test_entry(void)
{
    stm_sdram_t d = {0};
    SDRAM_HandleTypeDef h = fixture();
    CHECK(sdram_init(&d, &h, 64U) == STM_SDRAM_OK);
    CHECK(d.ready && d.base == 0xD0000000U && d.size_bytes == 32U * 1024U * 1024U);
    CHECK(d.clock_hz == 91666666U && d.refresh_count == 696U && refresh_value == 696U);
    CHECK(calls == 5U && delays >= 4U);
    CHECK(commands[0].CommandMode == FMC_SDRAM_CMD_CLK_ENABLE);
    CHECK(commands[1].CommandMode == FMC_SDRAM_CMD_PALL);
    CHECK(commands[2].CommandMode == FMC_SDRAM_CMD_AUTOREFRESH_MODE);
    CHECK(commands[2].AutoRefreshNumber == 8U);
    CHECK(commands[3].CommandMode == FMC_SDRAM_CMD_LOAD_MODE);
    CHECK(commands[3].ModeRegisterDefinition == 0x230U);
    for (unsigned i = 0; i < 4U; ++i) {
        CHECK(commands[i].CommandTarget == FMC_SDRAM_CMD_TARGET_BANK2);
    }
    CHECK(sdram_init(&d, &h, 64U) == STM_SDRAM_ERROR_CONFIG && calls == 5U);

    // 逐个注入命令和刷新设置失败，失败实例禁止访问 RAM。
    for (uint32_t at = 1U; at <= 5U; ++at) {
        for (uint32_t code = HAL_ERROR; code <= HAL_TIMEOUT; ++code) {
            h = fixture(); d = (stm_sdram_t){0};
            fail_call = at; injected_status = (HAL_StatusTypeDef)code;
            CHECK(sdram_init(&d, &h, 64U) == STM_SDRAM_ERROR_HAL);
            CHECK(!d.ready && calls == at && d.last_hal_status == injected_status);
            CHECK(sdram_fill16(&d, 0U, 0U, 1U) == STM_SDRAM_ERROR_NOT_READY);
            // 保留失败后的 HAL 状态，包括 PRECHARGED。
            fail_call = calls = 0U;
            CHECK(sdram_init(&d, &h, 64U) == STM_SDRAM_OK);
            CHECK(d.ready && calls == 5U && d.last_hal_status == HAL_OK);
        }
    }
    h = fixture(); d = (stm_sdram_t){0};
    CHECK(sdram_init(NULL, &h, 64U) == STM_SDRAM_ERROR_ARGUMENT);
    CHECK(sdram_init(&d, NULL, 64U) == STM_SDRAM_ERROR_ARGUMENT);
    CHECK(sdram_init(&d, &h, 0U) == STM_SDRAM_ERROR_ARGUMENT);
    CHECK(sdram_init(&d, &h, 1001U) == STM_SDRAM_ERROR_ARGUMENT);
    __disable_irq();
    stm_sdram_status_t context_status = sdram_init(&d, &h, 64U);
    __enable_irq();
    CHECK(context_status == STM_SDRAM_ERROR_CONTEXT && calls == 0U);
    __set_BASEPRI(0x80U);
    context_status = sdram_init(&d, &h, 64U);
    __set_BASEPRI(0U);
    CHECK(context_status == STM_SDRAM_ERROR_CONTEXT && calls == 0U);
    __set_FAULTMASK(1U);
    context_status = sdram_init(&d, &h, 64U);
    __set_FAULTMASK(0U);
    CHECK(context_status == STM_SDRAM_ERROR_CONTEXT && calls == 0U);
    const HAL_SDRAM_StateTypeDef invalid_states[] = {
        HAL_SDRAM_STATE_RESET, HAL_SDRAM_STATE_BUSY, HAL_SDRAM_STATE_ERROR,
        HAL_SDRAM_STATE_WRITE_PROTECTED, HAL_SDRAM_STATE_PRECHARGED
    };
    for (unsigned i = 0; i < sizeof(invalid_states) / sizeof(invalid_states[0]); ++i) {
        h.State = invalid_states[i];
        CHECK(sdram_init(&d, &h, 64U) == STM_SDRAM_ERROR_CONFIG && calls == 0U);
    }
    h.State = HAL_SDRAM_STATE_READY;
    kernel_clock = 0U;
    CHECK(sdram_init(&d, &h, 64U) == STM_SDRAM_ERROR_CONFIG && calls == 0U);
    kernel_clock = 275000000U;
    CHECK(sdram_init(&d, &h, 1000U) == STM_SDRAM_ERROR_CONFIG);
    h.Init.MemoryDataWidth = FMC_SDRAM_MEM_BUS_WIDTH_8;
    CHECK(sdram_init(&d, &h, 64U) == STM_SDRAM_ERROR_CONFIG);
    h.Init.MemoryDataWidth = FMC_SDRAM_MEM_BUS_WIDTH_16;
    h.Init.ReadBurst = FMC_SDRAM_RBURST_ENABLE;
    CHECK(sdram_init(&d, &h, 64U) == STM_SDRAM_ERROR_CONFIG);

    // 验证 Bank、行列、CAS 和时钟变化后的推导结果。
    h = fixture(); d = (stm_sdram_t){0};
    h.Init.SDBank = FMC_SDRAM_BANK1;
    h.Init.RowBitsNumber = FMC_SDRAM_ROW_BITS_NUM_12;
    h.Init.CASLatency = FMC_SDRAM_CAS_LATENCY_2;
    h.Init.SDClockPeriod = FMC_SDRAM_CLOCK_PERIOD_2;
    kernel_clock = 200000000U;
    CHECK(sdram_init(&d, &h, 64U) == STM_SDRAM_OK);
    CHECK(d.base == 0xC0000000U && d.size_bytes == 16U * 1024U * 1024U);
    CHECK(d.refresh_count == 1542U && commands[3].ModeRegisterDefinition == 0x220U);
    CHECK(commands[0].CommandTarget == FMC_SDRAM_CMD_TARGET_BANK1);

    h = fixture(); d = (stm_sdram_t){0};
    CHECK(sdram_init(&d, &h, 64U) == STM_SDRAM_OK);
    uint16_t tx[4] = {0x1234, 0x5678, 0xA5A5, 0xFFFF}, rx[4] = {0};
    CHECK(sdram_write16(&d, d.size_bytes - 8U, tx, 4U) == STM_SDRAM_OK);
    CHECK(sdram_read16(&d, d.size_bytes - 8U, rx, 4U) == STM_SDRAM_OK);
    for (unsigned i = 0; i < 4U; ++i) { CHECK(rx[i] == tx[i]); }
    CHECK(sdram_write16(&d, 0U, tx, SIZE_MAX) == STM_SDRAM_ERROR_RANGE);
    CHECK(sdram_write16(&d, d.size_bytes - 2U, tx, 2U) == STM_SDRAM_ERROR_RANGE);
    CHECK(sdram_read16(&d, UINT32_MAX - 1U, rx, 1U) == STM_SDRAM_ERROR_RANGE);
    CHECK(sdram_read16(&d, 1U, rx, 1U) == STM_SDRAM_ERROR_ARGUMENT);
    CHECK(sdram_write16(&d, 0U, NULL, 1U) == STM_SDRAM_ERROR_ARGUMENT);
    CHECK(sdram_write16(&d, 0U, (uint16_t *)((uintptr_t)tx + 1U), 1U)
          == STM_SDRAM_ERROR_ARGUMENT);
    CHECK(sdram_read16(&d, 0U, (uint16_t *)d.base, 1U) == STM_SDRAM_ERROR_ARGUMENT);
    CHECK(sdram_write16(&d, 0U, (uint16_t *)(d.base - 2U), 2U)
          == STM_SDRAM_ERROR_ARGUMENT);
    CHECK(sdram_read16(&d, 0U, (uint16_t *)(UINTPTR_MAX - 1U), 1U)
          == STM_SDRAM_ERROR_ARGUMENT);
    CHECK(sdram_fill16(NULL, 0U, 0U, 1U) == STM_SDRAM_ERROR_ARGUMENT);
    CHECK(sdram_fill16(&d, 1U, 0U, 0U) == STM_SDRAM_ERROR_ARGUMENT);
    CHECK(sdram_read16(&d, d.size_bytes, NULL, 0U) == STM_SDRAM_OK);
    CHECK(sdram_fill16(&d, d.size_bytes, 0U, 1U) == STM_SDRAM_ERROR_RANGE);
    CHECK(sdram_fill16(&d, 0U, 0x5AA5, 4U) == STM_SDRAM_OK);
    CHECK(sdram_read16(&d, 0U, rx, 4U) == STM_SDRAM_OK);
    for (unsigned i = 0; i < 4U; ++i) { CHECK(rx[i] == 0x5AA5); }
    CHECK(sdram_test(&d, 0U, 0U, NULL) == STM_SDRAM_ERROR_ARGUMENT);
    CHECK(sdram_test(&d, 0U, 3U, NULL) == STM_SDRAM_ERROR_ARGUMENT);
    stm_sdram_test_error_t error = {42U, 0xFFFF, 0xFFFF, 42U};
    CHECK(sdram_test(&d, d.size_bytes, 2U, &error) == STM_SDRAM_ERROR_RANGE);
    CHECK(error.offset_bytes == 0U && error.expected == 0U && error.actual == 0U && error.phase == 0U);
    // 子区域自检不得修改相邻保护字。
    CHECK(sdram_fill16(&d, 0U, 0xA55A, 8U) == STM_SDRAM_OK);
    CHECK(sdram_test(&d, 2U, 12U, &error) == STM_SDRAM_OK);
    CHECK(sdram_read16(&d, 0U, rx, 1U) == STM_SDRAM_OK && rx[0] == 0xA55A);
    CHECK(sdram_read16(&d, 14U, rx, 1U) == STM_SDRAM_OK && rx[0] == 0xA55A);
    // 验证跨 16 MiB 边界读写及容量末端自检。
    CHECK(sdram_write16(&d, 16U * 1024U * 1024U - 4U, tx, 4U) == STM_SDRAM_OK);
    CHECK(sdram_read16(&d, 16U * 1024U * 1024U - 4U, rx, 4U) == STM_SDRAM_OK);
    for (unsigned i = 0; i < 4U; ++i) { CHECK(rx[i] == tx[i]); }
    CHECK(sdram_test(&d, 0U, 256U * 1024U, NULL) == STM_SDRAM_OK);
    CHECK(sdram_test(&d, d.size_bytes - 4096U, 4096U, NULL) == STM_SDRAM_OK);
    return 0;
}

// 验证中断上下文禁止初始化
int test_interrupt_entry(void)
{
    stm_sdram_t d = {0};
    SDRAM_HandleTypeDef h = fixture();
    CHECK(sdram_init(&d, &h, 64U) == STM_SDRAM_ERROR_CONTEXT);
    CHECK(calls == 0U && delays == 0U && !d.ready);
    return 0;
}

// 验证 Python 注入的数据位固定故障。
int test_fault_entry(void)
{
    stm_sdram_t d = {0};
    stm_sdram_test_error_t e = {0};
    SDRAM_HandleTypeDef h = fixture();
    CHECK(sdram_init(&d, &h, 64U) == STM_SDRAM_OK);
    CHECK(sdram_test(&d, 0U, 4096U, &e) == STM_SDRAM_ERROR_VERIFY);
    CHECK(e.expected != e.actual && e.offset_bytes < 4096U);
    CHECK(!d.ready && sdram_fill16(&d, 0U, 0U, 1U) == STM_SDRAM_ERROR_NOT_READY);
    return 0;
}

// 验证 Python 注入的地址别名故障
int test_address_fault_entry(void)
{
    stm_sdram_t d = {0};
    stm_sdram_test_error_t e = {0};
    SDRAM_HandleTypeDef h = fixture();
    CHECK(sdram_init(&d, &h, 64U) == STM_SDRAM_OK);
    CHECK(sdram_test(&d, 0U, 4096U, &e) == STM_SDRAM_ERROR_VERIFY);
    CHECK(e.expected != e.actual && e.phase > 0U && e.offset_bytes < 4096U);
    CHECK(!d.ready && sdram_read16(&d, 0U, &e.actual, 1U) == STM_SDRAM_ERROR_NOT_READY);
    return 0;
}

// 回调测试上下文
typedef struct {
    uint32_t calls;      // 已收到的通知次数
    uint32_t cancel_at;  // 请求取消的通知序号，0 表示不取消
    uint64_t previous;   // 上次已完成工作量
    uint64_t total;      // 预期总工作量
    int invalid;        // 非零表示进度约束不满足
} progress_context_t;

// 验证进度单调、工作量及通知间隔，并按序号取消
static int check_progress(uint64_t completed, uint64_t total, void *user)
{
    progress_context_t *ctx = user;
    if (total != ctx->total || completed > total ||
        (ctx->calls == 0U && completed != 0U) ||
        (ctx->calls != 0U && (completed <= ctx->previous || completed - ctx->previous > 4096U))) {
        ctx->invalid = 1;
    }
    ctx->previous = completed;
    ++ctx->calls;
    return ctx->calls == ctx->cancel_at;
}

// 验证空上下文能传给回调
static int cancel_without_context(uint64_t completed, uint64_t total, void *user)
{
    (void)completed;
    (void)total;
    return user == NULL;
}

// 验证停用、恢复、进度以及各通知点的取消行为
int test_lifecycle_entry(void)
{
    stm_sdram_t d = {0};
    SDRAM_HandleTypeDef h = fixture();
    CHECK(sdram_disable(NULL) == STM_SDRAM_ERROR_ARGUMENT);
    CHECK(sdram_disable(&d) == STM_SDRAM_OK && calls == 0U);
    CHECK(sdram_init(&d, &h, 64U) == STM_SDRAM_OK);
    uintptr_t base = d.base;
    uint32_t count = calls;
    CHECK(sdram_disable(&d) == STM_SDRAM_OK && !d.ready);
    CHECK(sdram_disable(&d) == STM_SDRAM_OK && calls == count);
    CHECK(d.base == base && d.hal == &h && d.last_hal_status == HAL_OK);
    uint16_t data = 0;
    CHECK(sdram_read16(&d, 0U, &data, 1U) == STM_SDRAM_ERROR_NOT_READY);
    CHECK(sdram_write16(&d, 0U, &data, 1U) == STM_SDRAM_ERROR_NOT_READY);
    CHECK(sdram_test(&d, 0U, 2U, NULL) == STM_SDRAM_ERROR_NOT_READY);
    CHECK(sdram_init(&d, &h, 64U) == STM_SDRAM_OK);
    CHECK(d.ready && calls == count + 5U);

    progress_context_t ctx = {0};
    stm_sdram_test_error_t error;
    ctx.total = 8194U * 8U + 128U;
    CHECK(sdram_test_ex(&d, 2U, 8194U, &error, check_progress, &ctx) == STM_SDRAM_OK);
    CHECK(!ctx.invalid && ctx.calls == 26U && ctx.previous == ctx.total && d.ready);
    CHECK(error.offset_bytes == 0U && error.expected == 0U && error.actual == 0U && error.phase == 0U);

    // 覆盖起始、数据线、写入、校验、末次通知的取消。
    for (uint32_t cancel = 1U; cancel <= 26U; ++cancel) {
        ctx = (progress_context_t){0};
        ctx.total = 8194U * 8U + 128U;
        ctx.cancel_at = cancel;
        CHECK(sdram_fill16(&d, 0U, 0xA55AU, 4099U) == STM_SDRAM_OK);
        error = (stm_sdram_test_error_t){1U, 2U, 3U, 4U};
        CHECK(sdram_test_ex(&d, 2U, 8194U, &error, check_progress, &ctx) == STM_SDRAM_ERROR_CANCELLED);
        CHECK(!ctx.invalid && ctx.calls == cancel && d.ready);
        CHECK(error.offset_bytes == 0U && error.expected == 0U && error.actual == 0U && error.phase == 0U);
        CHECK(sdram_read16(&d, 0U, &data, 1U) == STM_SDRAM_OK && data == 0xA55AU);
        CHECK(sdram_read16(&d, 8196U, &data, 1U) == STM_SDRAM_OK && data == 0xA55AU);
        if (cancel == 1U) {
            CHECK(sdram_read16(&d, 2U, &data, 1U) == STM_SDRAM_OK && data == 0xA55AU);
        }
    }
    ctx = (progress_context_t){0};
    ctx.total = 144U;
    CHECK(sdram_test_ex(&d, 0U, 2U, NULL, check_progress, &ctx) == STM_SDRAM_OK);
    CHECK(!ctx.invalid && ctx.calls == 10U && ctx.previous == 144U);
    ctx.calls = 0U;
    CHECK(sdram_test_ex(&d, 0U, 3U, NULL, check_progress, &ctx) == STM_SDRAM_ERROR_ARGUMENT);
    CHECK(sdram_test_ex(&d, d.size_bytes, 2U, NULL, check_progress, &ctx) == STM_SDRAM_ERROR_RANGE);
    CHECK(ctx.calls == 0U && d.ready);
    CHECK(sdram_test_ex(&d, 0U, 2U, NULL, cancel_without_context, NULL) == STM_SDRAM_ERROR_CANCELLED);
    CHECK(sdram_disable(&d) == STM_SDRAM_OK);
    CHECK(sdram_test_ex(&d, 0U, 2U, NULL, check_progress, &ctx) == STM_SDRAM_ERROR_NOT_READY);
    CHECK(ctx.calls == 0U);
    return 0;
}

// 验证带回调的分块通知仍能发现地址别名并停用实例
int test_progress_fault_entry(void)
{
    stm_sdram_t d = {0};
    SDRAM_HandleTypeDef h = fixture();
    stm_sdram_test_error_t error;
    progress_context_t ctx = {0};
    ctx.total = 8194U * 8U + 128U;
    CHECK(sdram_init(&d, &h, 64U) == STM_SDRAM_OK);
    CHECK(sdram_test_ex(&d, 0U, 8194U, &error, check_progress, &ctx) == STM_SDRAM_ERROR_VERIFY);
    CHECK(!ctx.invalid && ctx.previous < ctx.total && error.phase > 0U && !d.ready);
    return 0;
}
