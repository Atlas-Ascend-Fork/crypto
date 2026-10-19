# Keccak 测试

简体中文 | [English](README_en.md)

| 测试 | 内容 | 依赖 |
| --- | --- | --- |
| keccak_launch_config_test | batch 边界和分核调度 | C++17 |
| crypto_direct_launch_test | 实际 Host 入口的参数校验、标量转发、运行时与同步错误传播 | ACL mock，无设备依赖 |
| crypto_public_headers_test | 公共接口可作为纯 C 编译 | C11 |
| keccak_golden / keccak_reference_kat | 已知答案及纯 C 参考实现 | Python / C11 |
| keccak_permutation_model | 向量置换算术模型与纯 C 参考比较 | C++17 |
| keccak_build_tools | Host-only 配置、Kernel UT 超时与 GTest ABI 检查 | CMake / Python |
| keccak_kernel_ut | 实际 Kernel 的 CPU 模拟与独立参考比较 | GTest、tikicpulib |
| st/test_keccak.cpp | kernel 直调与纯 C 参考逐字节比较 | CANN、A2 或 A3 |

设备用例 batch：1、7、8、9、64、255、256、257、1024、4096、8191、8192，保留非对齐 DMA 尾块回归。
CPU Kernel UT batch：1、7、8、64、255、256、257、320。

构建命令见 [build.md](../docs/build.md)。Host mock 不执行设备 kernel，设备正确性需在 Linux CANN 环境另行验证。
