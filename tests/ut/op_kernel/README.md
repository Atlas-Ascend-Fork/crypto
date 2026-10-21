# op_kernel

简体中文 | [English](README_en.md)

Ascend C 核函数用例：计算正确性、边界与尾块处理、多核调度。

CPU Kernel UT 使用 CANN 的 tikicpulib 与 ABI 匹配的 GTest，无需昇腾设备。真实设备的完整调用测试放在组件 `tests/st/`，通过 `build.sh --st` 运行。

对应各组件 `src/<组件>/tests/ut/op_kernel/` 下的同层用例，由 `build.sh -u --opkernel` 一并运行。
