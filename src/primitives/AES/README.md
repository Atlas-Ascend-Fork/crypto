# AES 原语

简体中文 | [English](README_en.md)

AES-128-CTR 和 AES-128-GCM 使用 Kernel 直调。核心共享库 `libcann_crypto_aes.so` 不依赖 PyTorch；现有 `torch.ops.ascend_ops` 接口由可选的 [torch_npu 适配层](../../../torch_extension/README.md)提供。

## 目录与接口

- `op_host/`：原生接口、参数检查、补零、Tiling、ACL 内存及同步。
- `op_kernel/`：CTR、GCM 的 Ascend C 实现和薄启动封装；保留 GCM 的四阶段执行。
- `tests/ut/op_host/`：CPU 上编译生产 Host 实现，以 ACL/启动 mock 检查状态传播、边界及资源释放。
- `tests/st/`：通过公共 C 接口运行 NPU，并与 PyCryptodome 对照；38 组用例，覆盖 CTR 长度边界、GCM 空消息与 AAD。
- `examples/`：原生 C++ 样例和不依赖框架的密钥展开脚本。
- `docs/`：[CTR](docs/ctr/README.md) 和 [GCM](docs/gcm/README.md) 的约束与贡献记录。

公共头文件为 [`include/cann_crypto/aes.h`](../../../include/cann_crypto/aes.h)，提供 `CannCryptoAes128Ctr` 和 `CannCryptoAes128Gcm`。输入输出使用连续设备内存，176 字节展开密钥、12 字节 nonce 及 GCM AAD 使用 Host 内存；调用方负责设备与 stream；输入生产任务须已完成或排在同一 stream 上。返回 ACL 状态码，成功返回时计算已完成。

现有语义保持：CTR 输出空间按 512 字节补齐，前原始长度字节为有效密文；GCM 标签位于补齐后的密文末尾，认证的是补零后的消息。当前 GCM 支持空消息或 2560 字节倍数的非空消息，以及最多 32 字节 AAD；其他长度的标签仍不保证正确，目录拆分没有更改 GHASH 算法。完整参数范围和输出容量要求见公共头文件。

## 构建和验证

```bash
# From the repository root; CANN is not needed for Host UT.
cmake -S src/primitives/AES -B build/aes-host -DCANN_BUILD_DEVICE=OFF
cmake --build build/aes-host --parallel
ctest --test-dir build/aes-host -L host --output-on-failure

# After sourcing the CANN set_env.sh; no PyTorch dependency.
cmake -S src/primitives/AES -B build/aes -DCANN_BUILD_DEVICE=ON -DNPU_ARCH=dav-2201
cmake --build build/aes --parallel
python3 -m pip install pycryptodome
ctest --test-dir build/aes -L device --output-on-failure
cmake --install build/aes --prefix "$PWD/install/aes"
```

设备 ST 需要 A2 NPU、CANN 和 PyCryptodome；Host UT 仅需 C++17 编译器与 CMake。`CANN_DEVICE_ID` 可选择测试设备。当前没有 AES CPU Kernel UT；Host UT 不执行设备计算，不能代替 ST。

安装结果包含 `lib/libcann_crypto_aes.so` 和 `include/cann_crypto/aes.h`。原生调用不需要安装 `torch`、`torch_npu` 或 `ascend_ops`。PyTorch wheel 构建与原来的 35 项回归见[适配层说明](../../../torch_extension/README.md)。
