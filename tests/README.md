# stm_sdram 验证

## 无 HAL 原生测试

```sh
cmake -S tests/portable -B build/portable -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/portable
ctest --test-dir build/portable --output-on-failure
```

只包含 C/C++ 标准库、组件公开头和 stm_common，不提供 HAL/CMSIS 路径或 MCU 宏。
覆盖独立上下文、器件配置、错误路径、边界与生命周期；Flash 另外覆盖自定义操作、显式 ID 不匹配、AUTO 歧义拒绝、传输分块和 tick 回绕；SDRAM 覆盖通用上电顺序、配置副作用前校验、参数快照和自检。

## 真实 HAL 结构与 ARM 模型回归

```sh
python tests/run_tests.py --include /path/to/hal-config --include /path/to/hal/Inc --include /path/to/device/Include --include /path/to/cmsis/Core/Include --mcu STM32H757xx  --build-dir build/h757-tests
```

运行器编译实际核心及选定适配器，使用 HAL 桩与 Unicorn Cortex-M7 执行 O0/O2/Os。H723 使用 STM32H723xx。可用 --compiler 指定 ARM GCC 路径。
需要 requirements.txt 中的 Python 依赖。测试适配代码模拟 BSP 传入有效时钟，不在核心注入 MCU 依赖。

保留原有容量、时序/协议、保护、超时、失败回收和数据故障测试；模拟不会验证电气、真实刷新保持时间或板级信号完整性。
开发板验证必须另行执行，未连接硬件时不能称为板测通过。公共发布前完成约定的板级验证。
