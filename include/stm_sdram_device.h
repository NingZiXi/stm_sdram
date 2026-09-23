/** @file stm_sdram_device.h @brief SDR SDRAM 几何、时序和刷新约束。 */
#ifndef STM_SDRAM_DEVICE_H
#define STM_SDRAM_DEVICE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        SDRAM_TMRD,
        SDRAM_TXSR,
        SDRAM_TRAS,
        SDRAM_TRC,
        SDRAM_TWR,
        SDRAM_TRP,
        SDRAM_TRCD,
        SDRAM_TIMING_COUNT
    };
    // 器件约束快照，create 会复制；时序索引使用 SDRAM_T*，单位分别为 ns 与时钟周期。
    typedef struct
    {
        const char *name;           // 可选只读名称；生命周期覆盖设备实例。
        uint32_t refresh_period_ms; // 全行刷新周期，1～1000 ms；由温度等级决定
        uint32_t startup_delay_ms;  // CLK_ENABLE 后最小等待，1～1000 ms
        uint32_t max_clock_hz;      // 当前工作条件允许的最高 SDCLK
        uint32_t timing_ns[7];      // tMRD、tXSR、tRAS、tRC、tWR、tRP、tRCD 的最小 ns
        uint8_t timing_cycles[7];   // 同一时序的最小周期；与 ns 要求取较大值
        uint8_t row_bits, column_bits, internal_banks, bus_width_bits;
        uint8_t cas_mask;            // bit N 表示允许 CAS N；由控制器适配器校验支持范围
        uint8_t auto_refresh_cycles; // 上电刷新命令次数，1～16
    } sdram_device_t;

    // 保守的 <=100 MHz、CAS3、16 位参考工作点，其他条件由用户提供配置。
    extern const sdram_device_t sdram_device_w9825g6kh_6;
    // IS42S32800J-7BLI：32 位，4K 行，工业级 64 ms 刷新，保守 <=100 MHz。
    extern const sdram_device_t sdram_device_is42s32800j_7;

#ifdef __cplusplus
}
#endif
#endif
