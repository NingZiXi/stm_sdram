/**
 * @file    test_sdram.c
 * @brief   使用 HAL 桩验证 SDRAM 驱动及故障处理
 */
#include "stm_sdram.h"
#include "tests/test_allocator.h"

#define CHECK(expr) do { if (!(expr)) { return __LINE__; } } while (0)

static uint32_t calls, delays, refresh_value, fail_call, kernel_clock;
static FMC_SDRAM_CommandTypeDef commands[4];
static HAL_StatusTypeDef injected_status;


// 用简短测试配置调用公开创建接口
static stm_err_t create_device(sdram_handle_t *out, SDRAM_HandleTypeDef *hal, uint32_t value)
{
    const sdram_config_t config = {.hal = hal, .refresh_period_ms = value};
    return sdram_create(&config, out);
}

// 查询公开诊断快照
static sdram_info_t info(sdram_handle_t handle)
{
    sdram_info_t result = {0};
    (void)sdram_get_info(handle, &result);
    return result;
}

// 填充测试运行时内存
void *memset(void *dst, int value, size_t size)
{
    unsigned char *out = dst;
    while (size-- != 0U) { *out++ = (unsigned char)value; }
    return dst;
}

// 模拟当前 HAL 不支持 FMC 频率查询
uint32_t HAL_RCCEx_GetPeriphCLKFreq(uint64_t clock)
{
    (void)clock;
    return 0U;
}

// 返回模拟 HCLK 频率
uint32_t HAL_RCC_GetHCLKFreq(void)
{
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
    __HAL_RCC_FMC_CONFIG(RCC_FMCCLKSOURCE_HCLK);
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
    sdram_handle_t d = NULL;
    SDRAM_HandleTypeDef h = fixture();
    CHECK(create_device(&d, &h, 64U) == STM_OK);
    CHECK(info(d).ready && info(d).base == 0xD0000000U && info(d).size_bytes == 32U * 1024U * 1024U);
    CHECK(info(d).clock_hz == 91666666U && info(d).refresh_count == 696U && refresh_value == 696U);
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
    CHECK(create_device(&d, &h, 64U) == STM_ERR_INVALID_STATE && calls == 5U);

    // 逐个注入命令和刷新设置失败，失败实例禁止访问 RAM。
    for (uint32_t at = 1U; at <= 5U; ++at) {
        for (uint32_t code = HAL_ERROR; code <= HAL_TIMEOUT; ++code) {
            h = fixture(); sdram_delete(&d);
            fail_call = at; injected_status = (HAL_StatusTypeDef)code;
            CHECK(create_device(&d, &h, 64U) == (injected_status == HAL_TIMEOUT ? STM_ERR_TIMEOUT : STM_ERR_IO));
            CHECK(d == NULL && calls == at && live_allocations == 0U);
            CHECK(sdram_fill16(d, 0U, 0U, 1U) == STM_ERR_INVALID_ARG);
            // 失败不保留对象；板级恢复 HAL 后重新创建。
            h.State = HAL_SDRAM_STATE_READY;
            fail_call = calls = 0U;
            CHECK(create_device(&d, &h, 64U) == STM_OK);
            CHECK(info(d).ready && calls == 5U && info(d).last_hal_status == HAL_OK);
        }
    }
    h = fixture(); sdram_delete(&d);
    CHECK(create_device(NULL, &h, 64U) == STM_ERR_INVALID_ARG);
    CHECK(create_device(&d, NULL, 64U) == STM_ERR_INVALID_ARG);
    CHECK(create_device(&d, &h, 0U) == STM_ERR_INVALID_ARG);
    CHECK(create_device(&d, &h, 1001U) == STM_ERR_INVALID_ARG);
    __disable_irq();
    stm_err_t context_status = create_device(&d, &h, 64U);
    __enable_irq();
    CHECK(context_status == STM_ERR_INVALID_CONTEXT && calls == 0U);
    __set_BASEPRI(0x80U);
    context_status = create_device(&d, &h, 64U);
    __set_BASEPRI(0U);
    CHECK(context_status == STM_ERR_INVALID_CONTEXT && calls == 0U);
    __set_FAULTMASK(1U);
    context_status = create_device(&d, &h, 64U);
    __set_FAULTMASK(0U);
    CHECK(context_status == STM_ERR_INVALID_CONTEXT && calls == 0U);
    const HAL_SDRAM_StateTypeDef invalid_states[] = {
        HAL_SDRAM_STATE_RESET, HAL_SDRAM_STATE_BUSY, HAL_SDRAM_STATE_ERROR,
        HAL_SDRAM_STATE_WRITE_PROTECTED, HAL_SDRAM_STATE_PRECHARGED
    };
    for (unsigned i = 0; i < sizeof(invalid_states) / sizeof(invalid_states[0]); ++i) {
        h.State = invalid_states[i];
        CHECK(create_device(&d, &h, 64U) == STM_ERR_INVALID_CONFIG && calls == 0U);
    }
    h.State = HAL_SDRAM_STATE_READY;
    kernel_clock = 0U;
    CHECK(create_device(&d, &h, 64U) == STM_ERR_INVALID_CONFIG && calls == 0U);
    kernel_clock = 275000000U;
    __HAL_RCC_FMC_CONFIG(RCC_FMCCLKSOURCE_PLL2);
    CHECK(create_device(&d, &h, 64U) == STM_ERR_INVALID_CONFIG);
    CHECK(!info(d).ready && calls == 0U && delays == 0U);
    __HAL_RCC_FMC_CONFIG(RCC_FMCCLKSOURCE_HCLK);
    CHECK(create_device(&d, &h, 1000U) == STM_ERR_INVALID_CONFIG);
    h.Init.MemoryDataWidth = FMC_SDRAM_MEM_BUS_WIDTH_8;
    CHECK(create_device(&d, &h, 64U) == STM_ERR_INVALID_CONFIG);
    h.Init.MemoryDataWidth = FMC_SDRAM_MEM_BUS_WIDTH_16;
    h.Init.ReadBurst = FMC_SDRAM_RBURST_ENABLE;
    CHECK(create_device(&d, &h, 64U) == STM_ERR_INVALID_CONFIG);

    // 验证 Bank、行列、CAS 和时钟变化后的推导结果。
    h = fixture(); sdram_delete(&d);
    h.Init.SDBank = FMC_SDRAM_BANK1;
    h.Init.RowBitsNumber = FMC_SDRAM_ROW_BITS_NUM_12;
    h.Init.CASLatency = FMC_SDRAM_CAS_LATENCY_2;
    h.Init.SDClockPeriod = FMC_SDRAM_CLOCK_PERIOD_2;
    kernel_clock = 200000000U;
    CHECK(create_device(&d, &h, 64U) == STM_OK);
    CHECK(info(d).base == 0xC0000000U && info(d).size_bytes == 16U * 1024U * 1024U);
    CHECK(info(d).refresh_count == 1542U && commands[3].ModeRegisterDefinition == 0x220U);
    CHECK(commands[0].CommandTarget == FMC_SDRAM_CMD_TARGET_BANK1);

    h = fixture(); sdram_delete(&d);
    CHECK(create_device(&d, &h, 64U) == STM_OK);
    uint16_t tx[4] = {0x1234, 0x5678, 0xA5A5, 0xFFFF}, rx[4] = {0};
    CHECK(sdram_write16(d, info(d).size_bytes - 8U, tx, 4U) == STM_OK);
    CHECK(sdram_read16(d, info(d).size_bytes - 8U, rx, 4U) == STM_OK);
    for (unsigned i = 0; i < 4U; ++i) { CHECK(rx[i] == tx[i]); }
    CHECK(sdram_write16(d, 0U, tx, SIZE_MAX) == STM_ERR_OUT_OF_RANGE);
    CHECK(sdram_write16(d, info(d).size_bytes - 2U, tx, 2U) == STM_ERR_OUT_OF_RANGE);
    CHECK(sdram_read16(d, UINT32_MAX - 1U, rx, 1U) == STM_ERR_OUT_OF_RANGE);
    CHECK(sdram_read16(d, 1U, rx, 1U) == STM_ERR_INVALID_ARG);
    CHECK(sdram_write16(d, 0U, NULL, 1U) == STM_ERR_INVALID_ARG);
    CHECK(sdram_write16(d, 0U, (uint16_t *)((uintptr_t)tx + 1U), 1U)
          == STM_ERR_INVALID_ARG);
    CHECK(sdram_read16(d, 0U, (uint16_t *)info(d).base, 1U) == STM_ERR_INVALID_ARG);
    CHECK(sdram_write16(d, 0U, (uint16_t *)(info(d).base - 2U), 2U)
          == STM_ERR_INVALID_ARG);
    CHECK(sdram_read16(d, 0U, (uint16_t *)(UINTPTR_MAX - 1U), 1U)
          == STM_ERR_INVALID_ARG);
    CHECK(sdram_fill16(NULL, 0U, 0U, 1U) == STM_ERR_INVALID_ARG);
    CHECK(sdram_fill16(d, 1U, 0U, 0U) == STM_ERR_INVALID_ARG);
    CHECK(sdram_read16(d, info(d).size_bytes, NULL, 0U) == STM_OK);
    CHECK(sdram_fill16(d, info(d).size_bytes, 0U, 1U) == STM_ERR_OUT_OF_RANGE);
    CHECK(sdram_fill16(d, 0U, 0x5AA5, 4U) == STM_OK);
    CHECK(sdram_read16(d, 0U, rx, 4U) == STM_OK);
    for (unsigned i = 0; i < 4U; ++i) { CHECK(rx[i] == 0x5AA5); }
    CHECK(sdram_test(d, 0U, 0U, NULL) == STM_ERR_INVALID_ARG);
    CHECK(sdram_test(d, 0U, 3U, NULL) == STM_ERR_INVALID_ARG);
    sdram_test_error_t error = {42U, 0xFFFF, 0xFFFF, 42U};
    CHECK(sdram_test(d, info(d).size_bytes, 2U, &error) == STM_ERR_OUT_OF_RANGE);
    CHECK(error.offset_bytes == 0U && error.expected == 0U && error.actual == 0U && error.phase == 0U);
    // 子区域自检不得修改相邻保护字。
    CHECK(sdram_fill16(d, 0U, 0xA55A, 8U) == STM_OK);
    CHECK(sdram_test(d, 2U, 12U, &error) == STM_OK);
    CHECK(sdram_read16(d, 0U, rx, 1U) == STM_OK && rx[0] == 0xA55A);
    CHECK(sdram_read16(d, 14U, rx, 1U) == STM_OK && rx[0] == 0xA55A);
    // 验证跨 16 MiB 边界读写及容量末端自检。
    CHECK(sdram_write16(d, 16U * 1024U * 1024U - 4U, tx, 4U) == STM_OK);
    CHECK(sdram_read16(d, 16U * 1024U * 1024U - 4U, rx, 4U) == STM_OK);
    for (unsigned i = 0; i < 4U; ++i) { CHECK(rx[i] == tx[i]); }
    CHECK(sdram_test(d, 0U, 256U * 1024U, NULL) == STM_OK);
    CHECK(sdram_test(d, info(d).size_bytes - 4096U, 4096U, NULL) == STM_OK);
    CHECK(sdram_delete(&d) == STM_OK);
    CHECK(live_allocations == 0U && invalid_frees == 0U);
    return 0;
}

// 验证中断上下文禁止初始化
int test_interrupt_entry(void)
{
    sdram_handle_t d = NULL;
    SDRAM_HandleTypeDef h = fixture();
    CHECK(create_device(&d, &h, 64U) == STM_ERR_INVALID_CONTEXT);
    CHECK(calls == 0U && delays == 0U && !info(d).ready);
    CHECK(sdram_delete(&d) == STM_OK);
    CHECK(live_allocations == 0U && invalid_frees == 0U);
    return 0;
}

// 验证 Python 注入的数据位固定故障。
int test_fault_entry(void)
{
    sdram_handle_t d = NULL;
    sdram_test_error_t e = {0};
    SDRAM_HandleTypeDef h = fixture();
    CHECK(create_device(&d, &h, 64U) == STM_OK);
    CHECK(sdram_test(d, 0U, 4096U, &e) == STM_ERR_VERIFY);
    CHECK(e.expected != e.actual && e.offset_bytes < 4096U);
    CHECK(!info(d).ready && sdram_fill16(d, 0U, 0U, 1U) == STM_ERR_INVALID_STATE);
    CHECK(sdram_delete(&d) == STM_OK);
    CHECK(live_allocations == 0U && invalid_frees == 0U);
    return 0;
}

// 验证 Python 注入的地址别名故障
int test_address_fault_entry(void)
{
    sdram_handle_t d = NULL;
    sdram_test_error_t e = {0};
    SDRAM_HandleTypeDef h = fixture();
    CHECK(create_device(&d, &h, 64U) == STM_OK);
    CHECK(sdram_test(d, 0U, 4096U, &e) == STM_ERR_VERIFY);
    CHECK(e.expected != e.actual && e.phase > 0U && e.offset_bytes < 4096U);
    CHECK(!info(d).ready && sdram_read16(d, 0U, &e.actual, 1U) == STM_ERR_INVALID_STATE);
    CHECK(sdram_delete(&d) == STM_OK);
    CHECK(live_allocations == 0U && invalid_frees == 0U);
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
    sdram_handle_t d = NULL;
    SDRAM_HandleTypeDef h = fixture();
    CHECK(sdram_delete(NULL) == STM_ERR_INVALID_ARG);
    CHECK(sdram_delete(&d) == STM_OK && calls == 0U);
    CHECK(create_device(&d, &h, 64U) == STM_OK);
    uint32_t count = calls;
    CHECK(sdram_delete(&d) == STM_OK && !info(d).ready);
    CHECK(sdram_delete(&d) == STM_OK && calls == count);
    CHECK(d == NULL && live_allocations == 0U);
    uint16_t data = 0;
    CHECK(sdram_read16(d, 0U, &data, 1U) == STM_ERR_INVALID_ARG);
    CHECK(sdram_write16(d, 0U, &data, 1U) == STM_ERR_INVALID_ARG);
    CHECK(sdram_test(d, 0U, 2U, NULL) == STM_ERR_INVALID_ARG);
    CHECK(create_device(&d, &h, 64U) == STM_OK);
    CHECK(info(d).ready && calls == count + 5U);

    progress_context_t ctx = {0};
    sdram_test_error_t error;
    ctx.total = 8194U * 8U + 128U;
    CHECK(sdram_test_ex(d, 2U, 8194U, &error, check_progress, &ctx) == STM_OK);
    CHECK(!ctx.invalid && ctx.calls == 26U && ctx.previous == ctx.total && info(d).ready);
    CHECK(error.offset_bytes == 0U && error.expected == 0U && error.actual == 0U && error.phase == 0U);

    // 覆盖起始、数据线、写入、校验、末次通知的取消。
    for (uint32_t cancel = 1U; cancel <= 26U; ++cancel) {
        ctx = (progress_context_t){0};
        ctx.total = 8194U * 8U + 128U;
        ctx.cancel_at = cancel;
        CHECK(sdram_fill16(d, 0U, 0xA55AU, 4099U) == STM_OK);
        error = (sdram_test_error_t){1U, 2U, 3U, 4U};
        CHECK(sdram_test_ex(d, 2U, 8194U, &error, check_progress, &ctx) == STM_ERR_CANCELLED);
        CHECK(!ctx.invalid && ctx.calls == cancel && info(d).ready);
        CHECK(error.offset_bytes == 0U && error.expected == 0U && error.actual == 0U && error.phase == 0U);
        CHECK(sdram_read16(d, 0U, &data, 1U) == STM_OK && data == 0xA55AU);
        CHECK(sdram_read16(d, 8196U, &data, 1U) == STM_OK && data == 0xA55AU);
        if (cancel == 1U) {
            CHECK(sdram_read16(d, 2U, &data, 1U) == STM_OK && data == 0xA55AU);
        }
    }
    ctx = (progress_context_t){0};
    ctx.total = 144U;
    CHECK(sdram_test_ex(d, 0U, 2U, NULL, check_progress, &ctx) == STM_OK);
    CHECK(!ctx.invalid && ctx.calls == 10U && ctx.previous == 144U);
    ctx.calls = 0U;
    CHECK(sdram_test_ex(d, 0U, 3U, NULL, check_progress, &ctx) == STM_ERR_INVALID_ARG);
    CHECK(sdram_test_ex(d, info(d).size_bytes, 2U, NULL, check_progress, &ctx) == STM_ERR_OUT_OF_RANGE);
    CHECK(ctx.calls == 0U && info(d).ready);
    CHECK(sdram_test_ex(d, 0U, 2U, NULL, cancel_without_context, NULL) == STM_ERR_CANCELLED);
    CHECK(sdram_delete(&d) == STM_OK);
    CHECK(sdram_test_ex(d, 0U, 2U, NULL, check_progress, &ctx) == STM_ERR_INVALID_ARG);
    CHECK(ctx.calls == 0U);
    CHECK(sdram_delete(&d) == STM_OK);
    CHECK(live_allocations == 0U && invalid_frees == 0U);
    return 0;
}

// 验证带回调的分块通知仍能发现地址别名并停用实例
int test_progress_fault_entry(void)
{
    sdram_handle_t d = NULL;
    SDRAM_HandleTypeDef h = fixture();
    sdram_test_error_t error;
    progress_context_t ctx = {0};
    ctx.total = 8194U * 8U + 128U;
    CHECK(create_device(&d, &h, 64U) == STM_OK);
    CHECK(sdram_test_ex(d, 0U, 8194U, &error, check_progress, &ctx) == STM_ERR_VERIFY);
    CHECK(!ctx.invalid && ctx.previous < ctx.total && error.phase > 0U && !info(d).ready);
    CHECK(sdram_delete(&d) == STM_OK);
    CHECK(live_allocations == 0U && invalid_frees == 0U);
    return 0;
}

// 验证对象所有权、内存不足、失败回收和配置快照
int test_handle_entry(void)
{
    SDRAM_HandleTypeDef h = fixture();
    sdram_handle_t d = NULL, second = NULL;
    sdram_config_t config = {.hal = &h, .refresh_period_ms = 64U};
    CHECK(sdram_create(NULL, &d) == STM_ERR_INVALID_ARG && d == NULL);
    CHECK(sdram_create(&config, NULL) == STM_ERR_INVALID_ARG);
    CHECK(sdram_get_info(NULL, NULL) == STM_ERR_INVALID_ARG);
    allocation_failure = 1;
    CHECK(sdram_create(&config, &d) == STM_ERR_NO_MEM);
    CHECK(d == NULL && live_allocations == 0U && calls == 0U);
    allocation_failure = 0;
    for (unsigned cycle = 0; cycle < 20U; ++cycle) {
        h = fixture();
        config.hal = &h;
        config.refresh_period_ms = 64U;
        CHECK(sdram_create(&config, &d) == STM_OK && d != NULL);
        CHECK(live_allocations == 1U);
        sdram_handle_t saved = d;
        uint32_t before = calls;
        CHECK(sdram_create(&config, &d) == STM_ERR_INVALID_STATE && d == saved);
        SDRAM_HandleTypeDef alias = h;
        config.hal = &alias;
        CHECK(sdram_create(&config, &second) == STM_ERR_INVALID_STATE && second == NULL);
        CHECK(calls == before && live_allocations == 1U);
        config.hal = NULL;
        config.refresh_period_ms = 0;
        CHECK(info(d).ready);
        CHECK(sdram_get_info(d, NULL) == STM_ERR_INVALID_ARG);
        __disable_irq();
        stm_err_t err = sdram_delete(&d);
        __enable_irq();
        CHECK(err == STM_ERR_INVALID_CONTEXT && d == saved && live_allocations == 1U);
        __set_BASEPRI(0x80U);
        err = sdram_delete(&d);
        __set_BASEPRI(0U);
        CHECK(err == STM_ERR_INVALID_CONTEXT && d == saved);
        __set_FAULTMASK(1U);
        err = sdram_delete(&d);
        __set_FAULTMASK(0U);
        CHECK(err == STM_ERR_INVALID_CONTEXT && d == saved);
        CHECK(sdram_delete(&d) == STM_OK && d == NULL && calls == before);
        CHECK(sdram_delete(&d) == STM_OK && live_allocations == 0U);
    }
    CHECK(allocation_calls > 20U && invalid_frees == 0U);
    return 0;
}
