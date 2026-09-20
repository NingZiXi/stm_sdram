# stm_sdram

基于 STM32H7 HAL FMC 的 **16 位 SDR SDRAM** 组件，提供不透明句柄、上电初始化、刷新配置、读写/填充和破坏性自检，统一返回 `stm_err_t`。

实测器件 **W9825G6KH-6（32 MiB）**。不依赖 RTOS、日志或 RTT；当前支持 CPU 同步访问、Normal non-cacheable MPU 映射，不提供 DMA、Cache 一致性维护或低功耗自刷新接口。

**v3.0.0** 将器件参数、FMC 平台实现和通用读写分开。创建时必须指定 `device`，可使用 `sdram_device_w9825g6kh_6` 预置或用户自定义 `sdram_device_t`。SDRAM 没有通用 JEDEC ID 自动识别，应用必须根据实际硬件选型填写；提供自定义参数不等于该芯片已获验证。

## 安装

需要 C11、CMake 3.22+、STM32H7 HAL/CMSIS 和 [stm_common](https://github.com/NingZiXi/stm_common)。使用 CMake 时可自动拉取公共依赖，只需在 STM32CubeMX 工程根目录克隆本组件：

```sh
git clone --branch v3.0.0 https://github.com/NingZiXi/stm_sdram.git Lib/stm_sdram
```

在 HAL 配置目标创建后加入：

```cmake
add_subdirectory(Lib/stm_sdram)
target_link_libraries(your_firmware PRIVATE stm_sdram)
```

也可用 FetchContent 替代上述克隆和 `add_subdirectory`，通过发布标签固定版本：

```cmake
include(FetchContent)
FetchContent_Declare(stm_sdram
    GIT_REPOSITORY https://gitee.com/nzxhg/stm_sdram.git
    GIT_TAG v3.0.0
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Lib/stm_sdram
)
FetchContent_MakeAvailable(stm_sdram)
target_link_libraries(your_firmware PRIVATE stm_sdram)
```

将 `your_firmware` 替换为实际固件目标。公共依赖按以下顺序解析：已有 `stm_common` target → 同级 `stm_common/` → FetchContent 下载 **v1.0.0** 对应的固定提交 `ce3d186dde2d374a8e9c7b9068a7b88f97d57dc1`。Flash 与 SDRAM 共用一个 target，不重复下载。

自动下载默认使用 GitHub，需要 Git 和网络；源码位于构建目录的 `_deps/stm_common-src`。如需使用 Gitee，在添加任意驱动前设置：

```cmake
set(STM_COMMON_GIT_REPOSITORY "https://gitee.com/nzxhg/stm_common.git" CACHE STRING "")
```

离线构建可提前提供 target 或同级目录，并设置 `STM_COMMON_FETCH=OFF` 禁止自动拉取；依赖缺失会在配置阶段明确报错。也可在启用自动依赖解析时通过 `FETCHCONTENT_SOURCE_DIR_STM_COMMON` 指定已有源码的绝对路径。切换仓库地址时更新 CMake 缓存或使用新的构建目录。手动提供的依赖版本由工程负责，建议使用 v1.0.0。
默认继承已有 `stm32cubemx` target 的 HAL 头文件和芯片宏；其他构建方式设置 `STM_SDRAM_LINK_CUBEMX=OFF`，自行提供 HAL/CMSIS 头文件、芯片宏及 HAL 实现。
Keil/IAR 工程手动加入 `stm_sdram.c`、`private/sdram_devices.c`、`private/sdram_fmc_h7.c`，将组件目录和 `stm_common` 加入 include 路径。
`STM_SDRAM_HAL_HEADER` 可指定其他 HAL 头文件，但不代表支持其他 STM32 系列。

组件对象仅在创建/删除时使用 `calloc/free`，读写不分配内存。须提供内部 RAM 堆，例如 STM32CubeMX 的 `sysmem.c` / `_sbrk`，不能把堆放在尚未初始化的外部存储中。

## 板级配置

先由板级初始化 HAL、GPIO、FMC；在首次 SDRAM 访问前配置 MPU 为 **Normal non-cacheable、不可执行**，HAL tick 须正常运行。支持 Bank1/Bank2 中一个活动 Bank，FMC 内核时钟当前仅支持 HCLK。

参考配置（STM32H723ZG + W9825G6KH-6）：

| 参数 | 配置 |
|---|---|
| SDBank / 基地址 | BANK2 / 0xD0000000 |
| 行 / 列 / 内部 Bank | 13 / 9 / 4 |
| 总线宽度 / 容量 | 16 位 / 32 MiB |
| CASLatency / ReadBurst | 3 / DISABLE |
| WriteProtection / ReadPipeDelay | DISABLE / 1 |
| SDClockPeriod | HCLK/3，HCLK 275 MHz 时约 91.67 MHz |
| 全行刷新周期 | 64 ms，计算得到刷新计数 696 |
| LoadToActiveDelay / ExitSelfRefreshDelay | 2 / 8 周期 |
| SelfRefreshTime / RowCycleDelay | 5 / 7 周期 |
| WriteRecoveryTime / RPDelay / RCDDelay | 3 / 2 / 2 周期 |

GPIO、行列和时序由 CubeMX 根据具体芯片配置，注意包括最高地址线和两根字节掩码 DQM。上述周期数只适用于对应时钟和芯片，改变时钟后须重新核对。
组件推导容量并计算刷新计数，向下取整再减 20 个 SDCLK；执行时钟使能、预充电、8 次自动刷新及模式寄存器加载。模式为 BL=1、顺序突发、单位置写突发。

预置参数是保守的 **不超过 100 MHz、CAS3、16 位**工作点，不代表芯片全部频率等级。创建前检查 HAL 行列、Bank、总线宽度、CAS 和频率，再读取 FMC SDTR 实际寄存器核对七项最小时序；Bank2 的 tRC/tRP 使用共享 SDTR1。周期要求取 `timing_cycles[i]` 与 `ceil(timing_ns[i] × SDCLK)` 的较大值，不会自动修改 CubeMX 的外设配置。

## 自定义器件参数

可以复制预置参数后按数据手册修改，例如更严格的温度刷新要求：

```c
sdram_device_t device = sdram_device_w9825g6kh_6;
device.refresh_period_ms = 32U;
const sdram_config_t config = {.hal = &hsdram1, .device = &device};
```

`device` 在创建时复制，调用后可释放或修改原配置；HAL 句柄仍须保持有效。参数包含行列、内部 Bank、总线宽度、CAS 掩码、最高时钟、全行刷新周期、上电等待和初始刷新次数。`timing_ns[7]` / `timing_cycles[7]` 的顺序为 tMRD、tXSR、tRAS、tRC、tWR、tRP、tRCD，每项至少指定一个非零要求。行刷新数当前按 `2^row_bits` 计算。

当前后端只接受 16 位、BL1、禁止读突发、CAS2/3 以及 HCLK 时钟源。其他总线宽度、模式和控制器需要新增平台实现与验证；不能仅修改参数绕过限制。`read16/write16/fill16` 的数量仍为 16 位元素数，不随物理总线宽度改变。

扩展时在 `private/sdram_devices.c` 增加经过核对的预置；FMC 时钟、寄存器和上电命令集中在 `private/sdram_fmc_h7.c`，通用生命周期、读写和自检位于 `stm_sdram.c`。

## 使用示例

板级初始化完成后：

```c
#include "stm_sdram.h"

sdram_handle_t ram = NULL;
const sdram_config_t config = {
    .hal = &hsdram1,
    .device = &sdram_device_w9825g6kh_6,
};
uint16_t tx[] = {0x1234U, 0xA55AU};
uint16_t rx[2] = {0};
stm_err_t err = sdram_create(&config, &ram);
if (err == STM_OK) {
    err = sdram_write16(ram, 0U, tx, 2U);
}
if (err == STM_OK) {
    err = sdram_read16(ram, 0U, rx, 2U);
}
if (err == STM_OK && (rx[0] != tx[0] || rx[1] != tx[1])) {
    err = STM_ERR_VERIFY;
}
stm_err_t cleanup = sdram_delete(&ram);
if (err == STM_OK) { err = cleanup; }
// 应用继续处理 err。
```

[example/main.c](example/main.c) 提供 `main()` 和集中的 `board_init()`，没有示例头文件、日志或 RTT。示例会覆盖起始 4 字节，完成后停留在空闲循环，通过 `example_result` 查看结果；默认不加入库编译。

## 接口

| 函数 | 行为 |
|---|---|
| sdram_create / sdram_delete | 创建初始化 / 释放句柄 |
| sdram_get_info | 查询行列、内部 Bank、总线宽度、CAS、基地址、容量、时钟、刷新计数、HAL 状态及 ready |
| sdram_read16 / sdram_write16 | 16 位元素读写 |
| sdram_fill16 | 用固定半字填充 |
| sdram_test | 数据线及地址模式自检，覆盖测试范围原数据 |
| sdram_test_ex | 带进度和取消回调的同类自检 |

读写偏移单位为**字节**，须为偶数；`count` 为 **uint16_t 元素个数**。缓冲区须 2 字节对齐、不与目标 SDRAM 区域重叠，推荐内部 RAM；零数量可传 NULL。

自检长度单位为字节，须为偶数且不少于 2。先执行 walking 1/0，再按整区写入和读取索引模式，检查数据线和地址混叠。失败返回首个偏移、期望值、实际值和阶段，并停用实例。
`sdram_test_ex` 回调返回非零可取消，返回 `STM_ERR_CANCELLED`；取消不恢复数据、不自动停用实例。回调在调用线程执行，不能重入或删除对象，不能访问测试区域。相邻通知最多完成 4096 字节读写工作量，不保证固定时间间隔；total=size_bytes*8+128，包含重复扫描。

## 生命周期与错误处理

- 输出句柄首次使用前设为 `NULL`。创建成功即可使用；失败释放内部对象，输出保持 NULL。已有句柄的输出变量会被拒绝并保留原值。
- 配置结构体只在创建时读取，HAL 句柄由应用持有，在设备整个生命周期内有效；使用期间不能重新配置其外设或时钟。
- `delete(&handle)` 成功后清空该变量，重复删除空句柄成功；其他句柄副本不会自动清空，删除后不可再使用。
- 删除只释放组件对象，不关闭 HAL 外设、不修改 MPU、不恢复存储数据；删除前先停止相关任务、DMA 和直接指针访问。
- 创建/删除须在普通线程上下文且中断开启时执行。应用须串行化同一组件的创建/删除，以及同一句柄的读写、查询和删除；组件不提供内部锁。
- 返回类型统一为 `stm_err_t`，`STM_OK=0`，用 `err != STM_OK` 判断错误。公共码定义见 [stm_common](https://github.com/NingZiXi/stm_common#错误码约定)。NULL 句柄返回 `STM_ERR_INVALID_ARG`，故障停用的有效句柄返回 `STM_ERR_INVALID_STATE`。
- `get_info` 返回只读快照；`ready` 表示软件允许访问，不代表硬件已自检通过。

整个 FMC SDRAM 控制器最多一个对象，包括故障停用对象。创建失败可能已改变硬件状态，HAL 为 PRECHARGED/BUSY/ERROR 时先由板级恢复到 READY，再重新创建。
自检失败后可查询信息，随后停止所有访问、删除对象、修复硬件或板级配置并重建。删除不会停止刷新或阻止直接指针访问。重新创建不保证旧数据有效。

HAL_TIMEOUT 映射为 STM_ERR_TIMEOUT，HAL_ERROR/HAL_BUSY 为 STM_ERR_IO。当前 H7 HAL 的 SendCommand 不使用 timeout 参数，组件使用命令间等待，不提供该函数的硬超时保证。普通 CPU 读写不依赖 HAL tick。

## 验证

- 软件：真实驱动在 Cortex-M7 模拟环境执行，O0/O2/Os 均通过，覆盖初始化、刷新计算、HAL 故障、边界、进度取消、数据位/地址混叠及对象生命周期；见 [tests/README.md](tests/README.md)。
- 实板基线（重构前 v2）：STM32H723ZG + W9825G6KH-6，约 91.67 MHz；20 次创建/删除、两轮全 32 MiB 自检、跨 16 MiB 读写以及三个区域的 2 秒保持检查通过。v3 已完成软件回归和编译检查，尚未重新进行硬件验证。
- 未覆盖：长期保持、温度范围、DMA/Cache、外部堆、自刷新及整板断电循环。其他板卡仍须独立验证。

规格参考：[W9825G6KH 数据手册](https://atta.szlcsc.com/upload/public/pdf/source/20170316/1489630415513.pdf)。

## 许可

组件原创代码采用 [MIT License](LICENSE)。`example/main.c` 中改编自 ST 的板级时钟初始化保留原版权及授权说明，见 [示例说明](example/README.md)；这些第三方部分不由本仓库重新授予 MIT 许可。HAL/CMSIS 不随组件分发，遵循各自许可。
