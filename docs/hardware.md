# STM32H723 / U6 SDRAM 硬件记录

2026-09-19 通过嘉立创 API 从当前 `STM32H723` 工程网表读取，未修改原理图。

U1：STM32H723ZGT6；U6：W9825G6KH-6（LCSC C62246），3.3 V。

## 信号对应

| U6 引脚 | SDRAM 信号 | 网名 | U1 GPIO | U1 引脚 |
|---|---|---|---|---|
| 2 | DQ0 | FMC_D0 | PD14 | 85 |
| 4 | DQ1 | FMC_D1 | PD15 | 86 |
| 5 | DQ2 | FMC_D2 | PD0 | 114 |
| 7 | DQ3 | FMC_D3 | PD1 | 115 |
| 8 | DQ4 | FMC_D4 | PE7 | 58 |
| 10 | DQ5 | FMC_D5 | PE8 | 59 |
| 11 | DQ6 | FMC_D6 | PE9 | 60 |
| 13 | DQ7 | FMC_D7 | PE10 | 63 |
| 15 | LDQM | FMC_NBL0 | PE0 | 141 |
| 16 | WE# | FMC_SDNWE | PA7 | 43 |
| 17 | CAS# | FMC_SDNCAS | PG15 | 132 |
| 18 | RAS# | FMC_SDNRAS | PF11 | 49 |
| 19 | CS# | FMC_SDNE1 | PB6 | 136 |
| 20 | BS0 | FMC_BA0 | PG4 | 89 |
| 21 | BS1 | FMC_BA1 | PG5 | 90 |
| 22 | A10/AP | FMC_A10 | PG0 | 56 |
| 23 | A0 | FMC_A0 | PF0 | 10 |
| 24 | A1 | FMC_A1 | PF1 | 11 |
| 25 | A2 | FMC_A2 | PF2 | 12 |
| 26 | A3 | FMC_A3 | PF3 | 13 |
| 29 | A4 | FMC_A4 | PF4 | 14 |
| 30 | A5 | FMC_A5 | PF5 | 15 |
| 31 | A6 | FMC_A6 | PF12 | 50 |
| 32 | A7 | FMC_A7 | PF13 | 53 |
| 33 | A8 | FMC_A8 | PF14 | 54 |
| 34 | A9 | FMC_A9 | PF15 | 55 |
| 35 | A11 | FMC_A11 | PG1 | 57 |
| 36 | A12 | FMC_A12 | PG2 | 87 |
| 37 | CKE | FMC_SDCKE1 | PB5 | 135 |
| 38 | CLK | FMC_SDCLK | PG8 | 93 |
| 39 | UDQM | FMC_NBL1 | PE1 | 142 |
| 42 | DQ8 | FMC_D8 | PA4 | 40 |
| 44 | DQ9 | FMC_D9 | PA5 | 41 |
| 45 | DQ10 | FMC_D10 | PE13 | 66 |
| 47 | DQ11 | FMC_D11 | PE14 | 67 |
| 48 | DQ12 | FMC_D12 | PC0 | 26 |
| 50 | DQ13 | FMC_D13 | PD8 | 77 |
| 51 | DQ14 | FMC_D14 | PD9 | 78 |
| 53 | DQ15 | FMC_D15 | PD10 | 79 |

A12=PG2，LDQM=PE0，UDQM=PE1。两根 DQM 均接 MCU，未固定接地。
初始 `.ioc` 未配置这三根线，行地址仅 12 位；现已用 CubeMX CLI 重新生成 FMC，配置为 13 位并使能 16-bit byte enable。

## 时序依据

W9825G6KH 第 15 页 **-6** 列（非 -6I/-6L）：tRCD/tRP≥15 ns、tRAS≥42 ns、tRC≥60 ns、tXSR≥72 ns、tWR≥2 cycles、tRSC≥2 cycles。
SDCLK=275 MHz/3，周期约 10.909 ns；当前保留以下保守参数：

| HAL 参数 | 周期数 | 实际时间（约） | 最小要求 |
|---|---|---|---|
| LoadToActiveDelay / tMRD | 2 | 21.82 ns | 2 cycles |
| ExitSelfRefreshDelay / tXSR | 8 | 87.27 ns | 72 ns |
| SelfRefreshTime / tRAS | 5 | 54.55 ns | 42 ns |
| RowCycleDelay / tRC | 7 | 76.36 ns | 60 ns |
| WriteRecoveryTime / tWR | 3 | 32.73 ns | 2 cycles；同时满足 FMC 组合约束 |
| RPDelay / tRP | 2 | 21.82 ns | 15 ns |
| RCDDelay / tRCD | 2 | 21.82 ns | 15 ns |

FMC 写恢复约束：TWR≥TRAS−TRCD=3，TWR≥TRC−TRCD−TRP=3，当前取 3。
原 SDCLK=137.5 MHz 时，原有 2/8/5/7/3/2/2 周期参数多项不足，因此改为 /3。
CAS=3，ReadBurst=DISABLE（配合 BL=1），ReadPipeDelay=1。改变时钟后必须重新核对。

## 初始化与复核

芯片需要 200 us 上电等待和 8 次刷新；组件使用 HAL_Delay(1)，在 H7 不等待完成的 HAL 命令之间也保守等待。
刷新计数 COUNT=696；32 MiB 区域以 Normal non-cacheable / XN 映射。
软件测试及编译通过不代表硬件通过。待实际烧录后检查 RTT 的全容量自检 PASS，继续做保持、重复读写及温度/供电裕量验证。

## CubeMX 生成证据

使用本机 CubeMX 6.17.0 的 `config load`、`config save`、`generate code`，只引入生成后的 `Core/Src/fmc.c` 和归一化 `.ioc`。
其他外设生成文件未替换。板级 MPU 和应用逻辑位于 `main/`，不依赖手工改写 FMC 生成区域。
本次生成脚本/日志和原始网表保存在被 Git 忽略的 `build/cubemx-sdram/`、`build/eda-netlist-response.json`，本文件保留必要硬件依据。
