# SHAKE 测试

简体中文 | [English](README_en.md)

- Host 测试覆盖启动参数、除零保护、尺寸溢出、分组调度、海绵模型及独立纯 C 参考实现。
- `shake_direct_launch_test` 验证公共直调入口的空指针、缓冲区重叠、标量传参及 ACL 错误传播。
- CPU Kernel UT 使用实际 SHAKE128、SHAKE256 Kernel，各包含四个参数化用例。
- `st/test_shake.cpp` 使用 OpenSSL EVP 验证 SHAKE128、SHAKE256 各 18 组参数；另验证 12 组 Keccak batch。
- 设备场景覆盖空消息、非对齐长度、rate 边界、多块吸收和输出扩展。
- `stress.py` 每轮重复运行两个算法各两个代表参数用例，日志保存在构建目录。

执行命令见 [build.md](../docs/build.md)。Host 测试无需 CANN；CPU Kernel UT 需要匹配 ABI 的 GTest 与 tikicpulib。

`ut/op_host/` 验证 Host 逻辑；`ut/op_kernel/` 中既有轻量模型测试，也有需要显式启用 `CANN_BUILD_CPU_DEBUG_TESTS=ON` 的实际 Kernel CPU 模拟。后者使用 CTest 标签 `kernel_ut`，在 CPU 上运行。`st/` 使用公开接口在真实 NPU 上执行完整调用路径，使用标签 `device`，需要 `CANN_BUILD_DEVICE=ON`。CPU 模拟通过不能替代设备 ST。
