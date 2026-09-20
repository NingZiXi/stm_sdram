# 软件测试

测试编译组件的真实 C 源码，用 HAL 桩和 Unicorn 执行 Cortex-M7 指令，不连接开发板。
不模拟 FMC 时序、刷新电路、MPU 或 Cache，不能据此判断硬件稳定性。

已验证工具：GNU Tools for STM32 GCC 12.3.1、Unicorn 2.1.4、pyelftools 0.32。
Python 依赖固定在 `requirements.txt`，推荐放在虚拟环境中：

以下命令在独立组件仓库根目录运行：

```powershell
python -m venv build/sdram-venv
build/sdram-venv/Scripts/python -m pip install -r tests/requirements.txt
build/sdram-venv/Scripts/python tests/run_tests.py --project-root D:/path/to/h7-project
```

运行器从组件位置向上寻找含 `Core/Inc` 和 H7 HAL 驱动目录的工程，不依赖调用时的工作目录。
组件迁出当前工程后，可以显式指定提供 HAL/CMSIS 头文件的 CubeMX 工程：

```powershell
python path/to/stm_sdram/tests/run_tests.py --project-root D:/path/to/h7-project --build-dir D:/temp/sdram-tests
```

目录结构不同的工程可重复传入 `--include`，分别指向 HAL、HAL 配置、CMSIS Core 和设备头文件目录。
显式 `--include` 且不指定 `--project-root` 时，不自动添加原工程目录。
`--compiler` 可指定 ARM GCC 路径，`--mcu` 可指定 H7 Cortex-M7 芯片宏，默认 `STM32H723xx`。
更换芯片宏只改变编译目标选择，不表示该芯片经过硬件验证。
`--python-deps` 可指定用 pip `--target` 安装依赖的目录，缺少依赖或头文件时返回明确错误。

测试覆盖初始化/重试、上下文、读写边界、数据线与地址别名故障、停用和恢复、进度单调性、
4096 字节通知间隔、尾块、全部通知点取消及失败自动停用，分别在 `-O0/-O2/-Os` 下运行。
测试失败以非零退出码和 C 文件行号报告。
