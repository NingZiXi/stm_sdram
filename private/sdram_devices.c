/**
 * @file    sdram_devices.c
 * @brief   SDR SDRAM 预置器件参数和参考工作点
 */
#include "stm_sdram.h"

const sdram_device_t sdram_device_w9825g6kh_6 = {
    .refresh_period_ms = 64U, .startup_delay_ms = 1U, .max_clock_hz = 100000000U,
    .timing_ns = {0U, 80U, 50U, 70U, 0U, 20U, 20U},
    .timing_cycles = {2U, 1U, 1U, 1U, 3U, 1U, 1U},
    .row_bits = 13U, .column_bits = 9U, .internal_banks = 4U, .bus_width_bits = 16U,
    .cas_mask = 1U << 3U, .auto_refresh_cycles = 8U,
};
