/** @file is42s32800j_7.c @brief 器件保守参考参数，来源沿用已核对的数据手册。 */
#include "stm_sdram_device.h"
const sdram_device_t sdram_device_is42s32800j_7 = {
    .name = "IS42S32800J_7",
    .refresh_period_ms = 64U,
    .startup_delay_ms = 1U,
    .max_clock_hz = 100000000U,
    .timing_ns = {14U, 70U, 49U, 70U, 14U, 20U, 20U},
    .timing_cycles = {2U, 1U, 1U, 1U, 2U, 1U, 1U},
    .row_bits = 12U,
    .column_bits = 9U,
    .internal_banks = 4U,
    .bus_width_bits = 32U,
    .cas_mask = (1U << 2U) | (1U << 3U),
    .auto_refresh_cycles = 8U,
};
