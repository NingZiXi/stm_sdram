# stm_sdram

**4.0.0：器件描述符、控制器接口与实例上下文重构版。已完成 H757 FMC/IS42S32800J-7 实板验证；H723 通过软件模型回归。**

同步 SDR SDRAM 初始化、16 位访问和显式自检组件。核心仅依赖 C 标准库与 `stm_common`，不包含 HAL/CMSIS；通过 **器件描述符 + controller 操作表 + 上下文** 接入。

## 结构与职责

- `include/stm_sdram.h`：应用 API。
- `include/stm_sdram_device.h`：几何、时序和刷新约束，以物理单位表达。
- `include/stm_sdram_controller.h`：控制器配置读回、命令、刷新与运行环境接口。
- `src/stm_sdram.c`：生命周期、范围检查、16 位读写。
- `src/sdram_init.c`：平台无关的几何/时序/刷新校验及 SDR SDRAM 上电步骤。
- `src/sdram_test.c`：显式调用的破坏性自检。
- `src/devices/`：IS42S32800J-7、W9825G6KH-6 独立参数文件。
- `adapters/stm32_hal/sdram_fmc.c`：FMC 编码和寄存器、HAL 错误转换，不包含器件选择。

时钟树、GPIO、控制器基础配置、MPU/Cache、双核协调和堆注册由 CubeMX/BSP 管理。
控制器适配器读取并校验已有配置，不擅自重配共享资源。FMC 适配器将通用刷新间隔减去硬件所需 20 周期余量，再校验并写入寄存器。

## CMake 与 BSP

```cmake
add_subdirectory(Lib/stm_sdram)
add_subdirectory(Lib/stm_sdram/adapters/stm32_hal)
target_compile_definitions(stm_sdram_fmc PUBLIC STM_SDRAM_HAL_HEADER="stm32h7xx_hal.h")
target_link_libraries(app PRIVATE stm_sdram_fmc)
```

默认仅构建核心。适配器显式加入；存在 `stm32cubemx` target 时继承其配置，其他构建方式显式提供 HAL 依赖。
已覆盖 H723/H757 HAL 编译及模型回归，其他系列需自行核对 HAL/FMC 差异并验证。
`STM_SDRAM_ENABLE_IS42S32800J_7`、`STM_SDRAM_ENABLE_W9825G6KH_6` 默认 ON，可关闭裁剪对应预置。

```c
#include "stm_sdram.h"
#include "sdram_fmc.h"

static sdram_fmc_context_t controller;
static sdram_handle_t ram;

stm_err_t board_sdram_init(uint32_t fmc_kernel_hz)
{
    const sdram_config_t config = {
        .device = &sdram_device_is42s32800j_7,
        .controller = sdram_fmc_bind(&controller, &hsdram1, fmc_kernel_hz),
    };
    return sdram_create(&config, &ram);
}
```

绑定前由 CubeMX/BSP 初始化 `hsdram1`，第三个参数是实际 FMC 内核时钟，适配器结合 SDCLK 分频检查器件时序。
绑定不初始化硬件。时钟源验证由 BSP 完成，不能把 CPU 频率或错误总线频率当成 FMC 时钟。
初始化顺序为时钟使能、等待、全 Bank 预充电、自动刷新、模式寄存器、自动刷新率。几何、时序、映射范围、控制器刷新范围等非法配置均在发出启动命令前拒绝。

## 器件与工作点

- IS42S32800J-7BLI：12 行、9 列、4 Bank、32 位，32 MiB；工业级 64 ms/4096 行；当前保守上限 100 MHz，CAS2/3。
- W9825G6KH-6：13 行、9 列、4 Bank、16 位，32 MiB；参考刷新 64 ms，保守上限 100 MHz，CAS3。
- H757 板的 80 MHz/CAS3 配置对应 FMC 刷新计数 1230。预置不会自动改动 CubeMX 时序，参数不匹配时报错。
- 芯片型号必须显式指定；不尝试从 FMC SDRAM 自动识别型号。自定义器件可以提供自己的 `sdram_device_t`。

## 生命周期与内存约束

create 复制器件数值参数和 controller 绑定，所以 config 和器件数值配置可为局部变量；可选名称字符串及 ops/context/底层句柄仍须保持有效。
同一个 FMC 的 Bank 共享控制器和刷新资源，FMC 适配器按整个外设独占。独立的其他控制器实例可各自创建对象。
仅 create 分配对象，读写过程不分配；delete 只释放对象，不关闭外设，也不关闭 SDRAM 刷新。
调用者串行化生命周期与全部内存访问；无默认互斥、RTOS、DMA 或双核一致性管理。

直接访问要求 BSP 已将映射配置为可访问的 **Normal non-cacheable、不可执行** 内存。控制器初始化成功不等于自动加入堆。
读写以 16 位元素计数，offset 是字节偏移；缓冲区不得与本实例映射重叠。写入屏障由控制器接口提供，核心不使用 ARM 指令。
自检会覆盖指定范围，必须显式调用。取消自检不恢复数据；数据校验失败停用实例。初始化不执行自检或清零整个 RAM。

## v3 → v4

1. `.hal` 改为 `.controller = sdram_fmc_bind(...)`，HAL 时钟来源移到 BSP。
2. 显式链接 `stm_sdram_fmc`；核心不再隐式包含 H7 HAL。
3. `sdram_info_t.last_hal_status` 改为 `last_error`；HAL 原始状态由适配器上下文的 `last_hal_status` 提供。
4. 手动集成改用 `include/`、`src/` 与所选适配器；重新编译所有使用公共结构体的消费者。

`stm_common` 自动复用 target/同级目录，否则固定 v1.0.0 提交；`STM_COMMON_FETCH=OFF` 可关闭下载。
验证见 [tests/README.md](tests/README.md)，原 H723 参考板示例见 [example/main.c](example/main.c)。示例时钟/引脚不能直接替代其他开发板 CubeMX 配置。
