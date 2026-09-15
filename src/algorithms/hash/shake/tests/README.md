# SHAKE 测试

## 单元测试

- `assets/golden.py`：通过 Python `hashlib` 计算独立参考结果，支持 SHAKE128 和 SHAKE256。
- `ut/op_host`：算法选择、尺寸校验、工作组调度及整数边界；分别调用两个算子的生产 Tiling 函数，检查结构体字段、tilingKey、workspace 和异常输入。
- `ut/op_kernel`：海绵结构的 Host 设计模型，与 `hashlib` 比较 234 个用例；编译后的纯 C 参考实现与 `hashlib` 比较 252 个用例；有 OpenSSL 时增加 OpenSSL 差分测试。

Host 设计模型不模拟 Ascend C 指令执行或同步行为。Keccak 置换单元测试由依赖模块提供。

## Kernel UT

`ut/op_kernel/test_shake_tensor.cpp` 使用 GTest 和 `tikicpulib`，分别执行 SHAKE128、SHAKE256
实际 Kernel，覆盖空消息、微块尾部、跨 rate 吸收和多块输出；期望结果由独立纯 C 参考生成。

使用 `-DCANN_BUILD_CPU_DEBUG_TESTS=ON` 启用，默认关闭，不参与普通 Host 测试。
按[Kernel UT 编译说明](../docs/build.md#kernel-ut)准备 ABI=0 的 GTest，
指定 `GTest_DIR` 并编译到 `build/cpu-debug`。命令在 SHAKE 目录执行。
此配置包含 Keccak、SHAKE128、SHAKE256 三个 Kernel UT 程序，不需要 NPU。
每个程序默认限时 1800 秒，可通过 `CANN_KERNEL_UT_TIMEOUT` 调整。

```bash
ctest --test-dir build/cpu-debug -L kernel_ut --output-on-failure --no-tests=error
```

## 设备正确性

`st/test_aclnn_sha3.cpp` 包含 5 个 Keccak 和 36 个 SHAKE 用例。
Keccak 采用纯 C 参考；SHAKE 采用 OpenSSL EVP 独立参考，逐字节比较结果。
覆盖空消息、rate 前后边界、多块吸收、多块输出及尾组。

```bash
ctest --test-dir build/cann-9.1.0-a2-sdk -L device --output-on-failure --no-tests=error
```

## 压力测试

以下示例使用 A2、CANN 9.1.0 构建目录；A3 使用 `build/cann-9.1.0-a3-sdk`。

先按[编译与运行](../docs/build.md)设置算子包路径，然后执行：

```bash
python3 tests/stress.py --build-dir build/cann-9.1.0-a2-sdk --device 0 --runs 100
```

每次启动独立进程，运行两个算法的 4 个代表形状。任一进程失败或超时即停止。
日志保存在所选构建目录的 `results/stress/` 下。
