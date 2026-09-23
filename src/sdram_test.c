/** @file sdram_test.c @brief 显式调用的破坏性自检，不在初始化中执行。 */
#include "sdram_internal.h"
// 记录首个数据不匹配并返回校验错误
static stm_err_t sdram_mismatch(sdram_handle_t dev, sdram_test_error_t *error, uint32_t offset_bytes,
                                uint16_t expected, uint16_t actual, uint32_t phase)
{
    if (error != NULL)
    {
        error->offset_bytes = offset_bytes;
        error->expected = expected;
        error->actual = actual;
        error->phase = phase;
    }
    dev->ready = 0U;
    dev->last_error = STM_ERR_VERIFY;
    return STM_ERR_VERIFY;
}

// 生成半字索引的低位、高位或反码测试值
static uint16_t sdram_pattern(uint32_t index, uint32_t phase)
{
    uint16_t value = (uint16_t)((phase < 2U) ? index : (index >> 16U));
    return (phase & 1U) != 0U ? (uint16_t)~value : value;
}

// 执行无回调的破坏性自检
stm_err_t sdram_test(sdram_handle_t dev, uint32_t offset_bytes, uint32_t size_bytes,
                     sdram_test_error_t *error)
{
    return sdram_test_ex(dev, offset_bytes, size_bytes, error, NULL, NULL);
}

// 执行带进度和取消回调的破坏性自检
stm_err_t sdram_test_ex(sdram_handle_t dev, uint32_t offset_bytes, uint32_t size_bytes,
                        sdram_test_error_t *error, sdram_progress_cb_t progress, void *user)
{
    if (error != NULL)
    {
        *error = (sdram_test_error_t){0};
    }
    if ((size_bytes & 1U) != 0U || size_bytes < 2U)
    {
        return STM_ERR_INVALID_ARG;
    }
    stm_err_t status = sdram_check_range(dev, offset_bytes, size_bytes / 2U);
    if (status != STM_OK)
    {
        return status;
    }
    const uint64_t total = (uint64_t)size_bytes * 8U + 128U;
    uint64_t completed = 0U;
    if (progress != NULL && progress(0U, total, user) != 0)
    {
        return STM_ERR_CANCELLED;
    }
    volatile uint16_t *memory = (volatile uint16_t *)(dev->base + offset_bytes);
    // 起始半字执行 walking 1/0，共 32 次写入和读回。
    for (uint32_t bit = 0U; bit < 16U; ++bit)
    {
        for (uint32_t inverse = 0U; inverse < 2U; ++inverse)
        {
            uint16_t expected = (uint16_t)(1U << bit);
            if (inverse != 0U)
            {
                expected = (uint16_t)~expected;
            }
            memory[0] = expected;
            sdram_barrier(dev);
            uint16_t actual = memory[0];
            if (actual != expected)
            {
                return sdram_mismatch(dev, error, offset_bytes, expected, actual, 0U);
            }
        }
    }
    completed = 128U;
    if (progress != NULL && progress(completed, total, user) != 0)
    {
        return STM_ERR_CANCELLED;
    }
    // 每轮先整区写入再整区校验，保留跨分块地址别名检测。
    for (uint32_t phase = 0U; phase < 4U; ++phase)
    {
        for (uint32_t i = 0U; i < size_bytes / 2U; ++i)
        {
            memory[i] = sdram_pattern(offset_bytes / 2U + i, phase);
            completed += 2U;
            if (progress != NULL && (((i + 1U) % 2048U) == 0U || i + 1U == size_bytes / 2U))
            {
                sdram_barrier(dev);
                if (progress(completed, total, user) != 0)
                {
                    return STM_ERR_CANCELLED;
                }
            }
        }
        sdram_barrier(dev);
        for (uint32_t i = 0U; i < size_bytes / 2U; ++i)
        {
            uint16_t expected = sdram_pattern(offset_bytes / 2U + i, phase);
            uint16_t actual = memory[i];
            if (actual != expected)
            {
                return sdram_mismatch(dev, error, offset_bytes + i * 2U, expected, actual, phase + 1U);
            }
            completed += 2U;
            if (progress != NULL && (((i + 1U) % 2048U) == 0U || i + 1U == size_bytes / 2U))
            {
                if (progress(completed, total, user) != 0)
                {
                    return STM_ERR_CANCELLED;
                }
            }
        }
    }
    return STM_OK;
}
