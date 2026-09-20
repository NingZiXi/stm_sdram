# stm_sdram 最小示例

`main.c` 先调用 `board_init()` 完成板级初始化，再演示库的三个步骤：初始化 SDRAM、写入两个 16 位数据、读回校验。不使用日志、RTT 或其他应用组件，也没有配套示例头文件。

`board_init()` 集中展示本板的 MPU、HAL、系统时钟、GPIO 和 FMC 初始化。时钟为 HSE 25 MHz、CPU 550 MHz、HCLK 275 MHz；SDRAM 为 Bank2、32 MiB、SDCLK=HCLK/3。GPIO 和 FMC 使用参考工程 CubeMX 生成的 `MX_GPIO_Init()`、`MX_FMC_Init()`，具体引脚和时序见 [板级配置](../README.md#板级配置)。`fmc.h`、`gpio.h` 及其实现由使用工程提供，不包含在组件仓库中。

这是供阅读和复制调用流程的参考代码，不加入工程构建。移植时按目标板调整 `board_init()` 及 FMC 配置。SDRAM 的 MPU 属性为 Normal non-cacheable、不可执行，栈和缓冲区须位于内部 RAM；HAL tick 必须正常运行。

`64U` 是 W9825G6KH-6 的刷新周期（毫秒）；偏移单位为字节，读写数量单位为 `uint16_t`。示例会覆盖 SDRAM 起始 4 字节。

示例使用 `sdram_config_t` 和 `sdram_create()` 创建对象，完成后 `sdram_delete(&device)` 清空句柄，在空闲循环中保留 `example_result`。`STM_OK` 表示读回一致，其他值为错误码；板级初始化失败在 `Error_Handler()` 停机。链接须提供 stm_common 和内部 RAM 堆（如 sysmem.c），不可使用未初始化的 SDRAM 作为堆。

板级时钟代码改编自参考 STM32H723 工程 `Core/Src/main.c`。原版权声明：Copyright (c) 2026 STMicroelectronics. All rights reserved. This software is licensed under terms that can be found in the LICENSE file in the root directory of this software component. If no LICENSE file comes with this software, it is provided AS-IS.

上述 ST 代码采用其原始授权条款，不适用本仓库原创代码的 MIT 授权。HAL/CMSIS、启动文件和 CubeMX 生成的外设文件由使用者的工程提供。
