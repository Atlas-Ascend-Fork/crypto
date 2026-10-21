# Native AES Invocation Example

English | [简体中文](README.md)

This C++ example calls the public C API for empty-message AES-128-GCM and checks the known 16-byte tag for an all-zero key and nonce. It does not require PyTorch. Nonempty GCM messages must be multiples of 2560 bytes; AAD is limited to 32 bytes.

```bash
# From the repository root, after loading the CANN environment.
cmake -S src/primitives/AES -B build/aes -DCANN_BUILD_DEVICE=ON
cmake --build build/aes --parallel
./build/aes/aes_gcm_example 0
```

[AES](../README_en.md) · [PyTorch](../../../../torch_extension/examples/README_en.md)
