# AES Primitives

English | [简体中文](README.md)

AES-128-CTR and AES-128-GCM use direct Kernel launches. The core shared library `libcann_crypto_aes.so` has no PyTorch dependency; the existing `torch.ops.ascend_ops` interfaces are provided by the optional [torch_npu integration](../../../torch_extension/README_en.md).

## Layout and interfaces

- `op_host/`: native entry points, validation, zero padding, Tiling, ACL memory and synchronization.
- `op_kernel/`: Ascend C CTR/GCM implementations and thin launch wrappers; GCM retains its four stages.
- `tests/ut/op_host/`: compile the production Host implementation on CPU and use ACL/launch mocks to check error propagation, boundaries, and resource cleanup.
- `tests/st/`: call the public C API on an NPU and compare against PyCryptodome; 38 cases cover CTR length boundaries, empty GCM messages, and AAD.
- `examples/`: a native C++ example and framework-independent key expansion helper.
- `docs/`: [CTR](docs/ctr/README_en.md) and [GCM](docs/gcm/README_en.md) constraints and contribution records.

The public header [`include/cann_crypto/aes.h`](../../../include/cann_crypto/aes.h) provides `CannCryptoAes128Ctr` and `CannCryptoAes128Gcm`. Input/output use contiguous device memory; the 176-byte expanded key, 12-byte nonce, and GCM AAD use Host memory. The caller owns the device and stream; input producers must have completed or be ordered on the same stream. Calls return ACL status codes and complete computation before returning success.

Existing semantics are preserved: CTR output capacity is rounded to 512 bytes, with only the original message length being valid ciphertext. The GCM tag follows the padded ciphertext and authenticates the zero-padded message. GCM currently supports empty messages or nonempty multiples of 2560 bytes, and up to 32 AAD bytes. Tags for other lengths remain unsupported; the layout change does not modify GHASH. See the public header for complete bounds and output capacity requirements.

## Build and validation

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

Device ST requires an A2 NPU, CANN, and PyCryptodome; Host UT only needs a C++17 compiler and CMake. Set `CANN_DEVICE_ID` to select the test device. AES currently has no CPU Kernel UT; Host UT does not execute device computation and cannot replace ST.

Installation provides `lib/libcann_crypto_aes.so` and `include/cann_crypto/aes.h`. Native calls do not require `torch`, `torch_npu`, or `ascend_ops`. See the [integration guide](../../../torch_extension/README_en.md) for wheel builds and the original 35 regression cases.
