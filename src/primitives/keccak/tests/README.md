# Keccak 测试

| 测试 | 内容 | 运行环境 |
| --- | --- | --- |
| assets/golden.py | 独立 Python 置换、全零初态已知答案、非法输入 | Python |
| test_build.py | 源码准备、CANN 框架路径查找、构建接口配置、ACLNN 包符号检查 | Python、CMake，使用框架替身 |
| ut/op_host | 生产 InferShape 和 Tiling：形状、分核、tilingKey、workspace、空指针 | C++17，轻量上下文替身 |
| ut/op_kernel/test_reference.c | 纯 C 参考实现的已知答案和负面用例 | C11 |
| ut/op_kernel/test_permutation.cpp | 生产置换头的 Host 算术模型，928 个状态 | C++17 |
| ut/op_kernel/test_keccak_f1600_tensor.cpp | 通过 ICPU_RUN_KF 调用实际 Kernel，与纯 C 参考比较 | Linux、GTest、CANN tikicpulib |
| st/test_aclnn_keccak.cpp | ACLNN 设备结果与纯 C 参考逐字节对比 | CANN、匹配编译目标的昇腾设备 |

Host 模型检查算术结果，不模拟 DMA、流水线同步或设备编译器。
设备测试覆盖 batch 为 64、256、1024、4096、8192。

在模块目录执行：

```bash
ctest --test-dir build/host --output-on-failure --no-tests=error
ctest --test-dir build/cann-9.1.0-a2-sdk -L device --output-on-failure --no-tests=error
```

设备测试示例使用 A2 构建目录；A3 使用 `build/cann-9.1.0-a3-sdk`。
编译配置见[编译与运行](../docs/build.md)。参考实现仅参与测试，不链接到生产算子包。

## Kernel UT

按[Kernel UT 编译说明](../docs/build.md#kernel-ut)准备 ABI=0 的 GTest，
指定 `GTest_DIR` 并编译到 `build/cpu-debug`。该配置默认关闭，不参与普通 Host 测试。
编译完成后在模块目录执行：

```bash
ctest --test-dir build/cpu-debug -L kernel_ut --output-on-failure --no-tests=error
```

`CANN_CPU_DEBUG_SOC` 使用 tikicpulib 提供的完整 SoC 名称，不是 `ascend910b` 等编译目标缩写。
每个 Kernel UT 程序默认限时 1800 秒，可通过 `CANN_KERNEL_UT_TIMEOUT` 调整。
CPU 调试不需要 NPU，但不替代设备验证。Host 上下文替身测试不替代目标仓库的 GTest/faker 集成测试。
