# SHAKE 测试

## 单元测试

- `assets/golden.py`：通过 Python `hashlib` 计算独立参考结果，支持 SHAKE128 和 SHAKE256。
- `ut/op_host`：算法选择、尺寸校验、工作组调度及整数边界；分别调用两个算子的生产 Tiling 函数，检查结构体字段、tilingKey、workspace 和异常输入。
- `ut/op_kernel`：海绵结构的 Host 设计模型，与 `hashlib` 比较 234 个用例；编译后的纯 C 参考实现与 `hashlib` 比较 252 个用例；有 OpenSSL 时增加 OpenSSL 差分测试。

Host 设计模型不模拟 Ascend C 指令执行或同步行为。Keccak 置换单元测试由依赖模块提供。

## Kernel UT

`ut/op_kernel/test_shake_tensor.cpp` 使用 GTest 和 `tikicpulib`，分别执行 SHAKE128、SHAKE256
实际 Kernel，覆盖空消息、微块尾部、跨 rate 吸收和多块输出；期望结果由独立纯 C 参考生成。

使用 `-DCANN_BUILD_CPU_DEBUG_TESTS=ON` 启用，默认关闭。配置方法见
[Keccak Kernel UT](../../../../primitives/keccak/tests/README.md#kernel-ut)，命令在 SHAKE 目录执行。
此配置同时包含 Keccak Kernel UT，不需要 NPU；GTest 必须与 CANN CPU 调试库的 C++ ABI 一致。

## 设备正确性

`st/test_aclnn_sha3.cpp` 包含 5 个 Keccak 和 36 个 SHAKE 用例。
Keccak 采用纯 C 参考；SHAKE 采用 OpenSSL EVP 独立参考，逐字节比较结果。
覆盖空消息、rate 前后边界、多块吸收、多块输出及尾组。

```bash
(cd build/device && ctest --output-on-failure)
```

## 压力测试

先按[编译与运行](../docs/build.md)设置算子包路径，然后执行：

```bash
python3 tests/stress.py --build-dir build/device --device 0 --runs 100
```

每次启动独立进程，运行两个算法的 4 个代表形状。任一进程失败或超时即停止。
日志保存在 `build/device/results/stress/`。
