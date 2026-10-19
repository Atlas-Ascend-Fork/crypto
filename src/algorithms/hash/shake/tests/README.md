# SHAKE 测试

- Host 测试覆盖启动参数、除零保护、尺寸溢出、分组调度、海绵模型及独立纯 C 参考实现。
- `shake_direct_launch_test` 验证公共直调入口的空指针、缓冲区重叠、标量传参及 ACL 错误传播。
- CPU Kernel UT 使用实际 SHAKE128、SHAKE256 Kernel，各包含四个参数化用例。
- `st/test_shake.cpp` 使用 OpenSSL EVP 验证 SHAKE128、SHAKE256 各 18 组参数；另验证 12 组 Keccak batch。
- 设备场景覆盖空消息、非对齐长度、rate 边界、多块吸收和输出扩展。
- `stress.py` 每轮重复运行两个算法各两个代表参数用例，日志保存在构建目录。

执行命令见 [build.md](../docs/build.md)。Host 测试无需 CANN；CPU Kernel UT 需要匹配 ABI 的 GTest 与 tikicpulib。
