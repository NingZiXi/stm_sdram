# stm_sdram

STM32H7 FMC 的 16 位 SDR SDRAM 独立组件，参考板使用 **W9825G6KH-6** 配置。
参考 `stm_log` 的组件方式：独立静态库、公开头文件和 CMake target。
不依赖日志库、RTOS、动态内存，也不绑定全局 `hsdram1`。

仓库：<https://github.com/NingZiXi/stm_sdram>。本仓库仅包含组件、参考示例和测试；HAL/CMSIS、CubeMX 生成文件及完整应用由使用工程提供。

## 文件与职责

| 文件 | 内容 |
|---|---|
| `stm_sdram.c/.h` | 上电命令、刷新率计算、半字读写/填充、破坏性自检 |
| `CMakeLists.txt` | `stm_sdram` 静态库，自动传递 `stm32cubemx` 接口依赖 |
| `docs/hardware.md` | 从当前嘉立创原理图读取的接线、型号及配置依据 |
| `tests/` | 用实际 ARM 编译产物运行的 HAL mock 和内存故障测试 |
| `CHANGELOG.md` | 接口和行为变化记录 |
| `example/` | 仅展示初始化、写入和读回校验的最小参考示例 |

参考工程的 FMC/GPIO 由 `.ioc` + CubeMX 生成，MPU、实例和日志位于参考工程的 `main/app_sdram.c`。这些文件不包含在独立组件仓库中；板级初始化参考本仓库 `example/main.c`。

### 为什么没有独立 config.h

当前组件没有需要单独配置文件管理的功能开关。1 ms 启动等待、8 次自动刷新属于启动协议约束，
已收在 `.c` 内部；版本和可覆盖的 `STM_SDRAM_HAL_HEADER` 放在公开头文件中。
本版本 H7 HAL 的 `SendCommand` 不使用 timeout 参数，因此也不把它包装成可调的超时保障。

真正的配置各有明确位置：引脚/几何参数/时序在 `.ioc`，刷新周期通过 `init` 参数传入，
MPU 在板级应用中，是否运行启动自检通过 CMake 的 `CONFIG_SDRAM_TEST_ENABLED` 控制。
这些信息不会再在组件配置头里重复一份。

## 参考板配置（来自 stm_h723_demo）

- U6：Winbond **W9825G6KH-6**，256 Mbit = **32 MiB**，4 banks × 8192 rows × 512 columns × 16 bits。
- FMC Bank2，`0xD0000000` 至 `0xD1FFFFFF`；`hsdram1` 是 HAL 变量名称，不代表 Bank1。
- 行地址 13 位、列地址 9 位，CAS=3，BL=1，顺序突发，写突发为单位置。
- FMC kernel = HCLK = 275 MHz，SDCLK = HCLK / 3 ≈ **91.666667 MHz**。
- 刷新要求：8192 次 / 64 ms。COUNT = floor(275000000 × 64 / (3 × 1000 × 8192)) − 20 = **696**。
- Region 1 专用于这 32 MiB，MPU 属性为 **Normal、non-cacheable、shareable、不可执行**。
  覆盖 CubeMX Region 0 的禁止访问设置。外部 RAM 未加入链接脚本的启动 `.data/.bss`、heap 或 stack。

启动流程：`MX_FMC_Init()` → `app_main()` 初始化日志 → `app_sdram_init()` → SDRAM 命令序列 → MPU → 自检。
默认执行一次全 32 MiB 破坏性自检，然后进入 LED 主循环；自检会增加启动时间。
自检失败输出首个错误的阶段、偏移、期望值和实际值，并清除 `ready`；LED 演示继续运行。

关闭启动自检：

```sh
cmake --preset Debug -DCONFIG_SDRAM_TEST_ENABLED=OFF
cmake --build --preset Debug
```

## 接入 STM32H7 工程

将本仓库克隆到使用工程的 `Lib/stm_sdram` 后纳入构建；也可通过 CMake FetchContent 获取并固定提交 SHA。私有仓库需要配置 GitHub Git 访问权限。

最小用法参考见 [example/README.md](example/README.md)。

```cmake
add_subdirectory(Lib/stm_sdram)
target_link_libraries(${CMAKE_PROJECT_NAME} stm_sdram)
```

若 HAL 接口 target 不是 `stm32cubemx`，设置 `STM_SDRAM_LINK_CUBEMX=OFF`，自行给组件提供 HAL
头文件路径、芯片宏和 HAL 实现。`STM_SDRAM_HAL_HEADER` 可覆盖头文件名称；本实现使用 H7 的
`HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_FMC)`，不宣称直接兼容其他系列。

初始化前必须满足：

1. 电源、时钟、GPIO、FMC 几何参数及所有时序已按器件配置，HAL 状态为 READY。
2. 在 SysTick 可运行的主循环/任务上下文调用；不能在中断内或关中断时调用。
3. 在首次内存访问前配置 MPU 为 Normal non-cacheable。不能只启用 MPU 全局默认映射：
   本工程 Region 0 会屏蔽 SDRAM 地址区。
4. 驱动不改变外部时钟或 FMC 时序。更换芯片、改变系统频率后，必须重新核对时序和刷新周期。
5. 本组件独占 FMC SDRAM 控制器，使用一个 Bank；不支持另一个 Bank 同时工作，因为两 Bank 共用时钟和刷新寄存器。

```c
#include "stm_sdram.h"
static stm_sdram_t ram;  /* 必须零初始化，整个使用期间保持有效 */

/* MX_FMC_Init() 完成后；其他器件按数据手册传入刷新周期。 */
stm_sdram_status_t status = sdram_init(&ram, &hsdram1, 64U);
if (status == STM_SDRAM_OK) {
    board_sdram_mpu_init(); /* 由移植工程实现，参考 main/app_sdram.c */
    uint16_t tx[] = {0x1234, 0xABCD};
    uint16_t rx[2];
    status = sdram_write16(&ram, 0, tx, 2);
    if (status == STM_SDRAM_OK) {
        status = sdram_read16(&ram, 0, rx, 2);
    }
}
```

参考工程提供 `app_sdram` 实例；独立使用时由应用创建实例并完成板级配置。

## API 约定

| API | 行为 |
|---|---|
| `sdram_init(dev, hal, refresh_period_ms)` | 时钟使能、等待、全 Bank 预充电、8 次自动刷新、加载模式、设置周期刷新 |
| `sdram_disable(dev)` | 停用接口访问，保留诊断信息，不修改硬件配置 |
| `sdram_write16(dev, offset, src, count)` | 从内部 RAM 写入 SDRAM |
| `sdram_read16(dev, offset, dst, count)` | 从 SDRAM 读入内部 RAM |
| `sdram_fill16(dev, offset, value, count)` | 用 16 位值填充 |
| `sdram_test(dev, offset, size_bytes, error)` | 覆盖指定区域并验证，错误结构可传 NULL |
| `sdram_test_ex(dev, offset, size_bytes, error, progress, user)` | 与普通自检相同，增加进度与取消回调 |

`offset` 为字节偏移，必须是偶数；读写的 `count` 为 **16 位元素数**，测试的 `size_bytes` 为字节数。
缓冲区必须对齐且不能与 SDRAM 重叠。零长度读写允许空指针；边界检查用减法/除法避免整数溢出。
重复初始化已经 ready 的实例返回 CONFIG，不会重新发命令。HAL 失败保留 `last_hal_status`，`ready` 保持 0。
修复失败原因后，可用同一实例和 HAL 句柄重试；支持中途留在 PRECHARGED 的情况。
如果 HAL 为 BUSY/ERROR/RESET，则先由板级恢复外设，组件不会强行把句柄改成 READY。
状态码区分参数错误、配置错误、HAL 错误、未初始化、越界、数据校验错误和调用上下文错误。
扩展自检另有 `STM_SDRAM_ERROR_CANCELLED`，表示回调主动取消。
初始化在 ISR 中或 PRIMASK/BASEPRI/FAULTMASK 非零时返回 CONTEXT，且不会发出命令。
仍需调用者保证 tick 正常运行；此检查不能检测 `HAL_SuspendTick()` 或调度器对 tick 的所有修改。

读写为 CPU 同步访问，无 DMA、无锁；调用者负责串行化。不要在 DMA 或其他任务使用同一内存时执行自检。
直接指针访问绕过边界检查；字节/非对齐访问不属于该接口。当前板 LDQM/UDQM 已正确连接并配置，
组件采用半字访问以匹配总线宽度，并避免测试被 `memcpy` 的宽度优化改变。

自检阶段 0：首个半字 walking-1/0，检查数据线；阶段 1–4：分别用地址索引的低/高 16 位及其反码，
每阶段先整区写入、再整区校验，以发现地址线别名（包括 16 MiB 边界）、常见数据线和存储错误。
非空错误结构在入口清零；仅 VERIFY 状态下的字段表示数据校验故障。
测试是破坏性的，不保留原数据，也不是完整的 March/压力测试。必须使用 non-cacheable 映射，
否则可能只验证到 CPU Cache。若日后启用 SDRAM Cache，应另行设计 DMA 一致性及测试的缓存维护策略。

## 状态与恢复

`ready` 只表示当前实例允许通过库接口访问，不代表物理内存已通过测试。字段由库维护，应用不直接写入。

| 操作结果 | ready | 含义 |
|---|---|---|
| 初始化成功 | 1 | 上电命令和刷新设置成功，尚未验证数据存储 |
| 自检成功 | 保持 1 | 仅所选区域本次测试通过 |
| 自检数据不匹配 | 0 | 自动停用，保留错误位置和 HAL 诊断信息 |
| 参数错误或主动取消 | 不变 | 未获得测试通过结论，取消前的数据可能已经被覆盖 |
| `sdram_disable()` | 0 | 软件停止接受读写和自检，不修改 FMC、MPU、GPIO 或刷新 |

恢复流程：停止其他任务、DMA 和直接指针访问，调用 `sdram_disable()`，修复板级故障，再调用
`sdram_init()` 和所需范围的自检。HAL 为 BUSY/ERROR/RESET 时须先由板级恢复外设。
重新初始化不保证旧数据有效；停用也不会使已有指针失效或阻止 CPU 直接访问。
同一 FMC 控制器只能交给一个实例管理，这一约束由应用保证。

## 自检进度与取消

保留 `sdram_test()` 的原调用方式；需要进度时使用 `sdram_test_ex()`：

```c
static int test_progress(uint64_t completed, uint64_t total, void *user)
{
    const volatile int *cancel = user;
    (void)completed;
    (void)total;
    // 可在这里调用应用自己的进度显示或喂狗函数。
    return *cancel != 0;
}

volatile int cancel = 0;
stm_sdram_test_error_t error;
stm_sdram_status_t status = sdram_test_ex(&ram, 0U, ram.size_bytes,
                                        &error, test_progress, (void *)&cancel);
```

回调在调用线程中同步执行，不能重入组件、停用实例或访问被测区域。回调返回 0 继续，非零取消。
取消返回错误码，测试数据不会回滚；如取消后业务不允许继续使用内存，可显式调用 `sdram_disable()`。
首次通知在内存访问前发生；后续在数据线测试完成后以及每读写最多 4096 字节时发生，包含末尾不足一块的部分。
通知间隔按工作量限制，不是实时保证。喂狗间隔必须在实际板卡、编译配置和回调负载下测量。
`completed/total` 是包含重复扫描的读写工作量，`total = size_bytes * 8 + 128`，不是物理容量。
最后一次通知也允许取消；仅 API 返回 `STM_SDRAM_OK` 才表示本次自检通过。

## 支持范围

- 当前目标为 STM32H7 FMC，软件验证使用 H723 HAL；不承诺只替换 HAL 头文件就能支持 F4/F7。
- 仅支持 16 位总线、一个活动 Bank、CPU 同步读写、Normal non-cacheable MPU 映射。
- 时钟改变前停止全部访问；重新配置 FMC 时序并重新初始化刷新参数后再访问，不能只修改刷新计数。
- CMake target 要求 C11；HAL/CMSIS 头文件、芯片宏和实现由使用工程提供。
- 尚未完成开发板验证。DMA、Cache、外部堆和低功耗自刷新留待具体应用需求确定。

## 版本与许可

当前组件版本为 `1.1.0`，变更见 [CHANGELOG.md](CHANGELOG.md)。
组件自身的分发许可证尚未确定，当前未声明为 MIT、BSD 或其他开源授权。
示例中源自 ST 的板级代码保留原有授权说明，见 [example/README.md](example/README.md)。

## 验证与来源

```sh
python -m pip install -r tests/requirements.txt
python tests/run_tests.py --project-root /path/to/stm32h7-project
```

测试编译实际驱动源码并运行 Cortex-M 指令，HAL 函数用 mock 替代；覆盖初始化命令顺序、Bank/CAS、
刷新计算、每一步 HAL 失败与重试、ISR/中断屏蔽检查、整数溢出/越界/对齐、跨 16 MiB 边界读写、
子区间自检不越界，以及注入的数据位故障/地址别名。覆盖 `-O0`、`-O2`、`-Os` 三种优化级别。
这不能代替板级电气、刷新保持、温度或高速稳定性验证。

软件验证记录和下一步上板检查见 [验证说明](docs/validation.md)。
测试运行器的独立移植参数见 [tests/README.md](tests/README.md)。

规格依据：[Winbond W9825G6KH，2016-06-01 Revision A03](https://atta.szlcsc.com/upload/public/pdf/source/20170316/1489630415513.pdf)，
第 3 页容量/刷新，第 7 页初始化，第 15 页 -6 速度等级时序。
FMC/HAL 接口依据工程自带 STM32Cube H7 V1.13.0 实现。

组件通过本仓库独立管理；使用工程应固定依赖提交，避免构建随分支更新而变化。
