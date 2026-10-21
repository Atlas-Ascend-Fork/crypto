# AES 原生调用样例

简体中文 | [English](README_en.md)

此 C++ 样例通过公共 C API 对空消息执行 AES-128-GCM，校验全零密钥与 nonce 的已知 16 字节标签。无需 PyTorch。非空 GCM 消息须为 2560 字节倍数；AAD 最多 32 字节。

```bash
# From the repository root, after loading the CANN environment.
cmake -S src/primitives/AES -B build/aes -DCANN_BUILD_DEVICE=ON
cmake --build build/aes --parallel
./build/aes/aes_gcm_example 0
```

[AES](../README.md) · [PyTorch](../../../../torch_extension/examples/README.md)
