# KeccakF1600Tensor

English | [简体中文](README.md)

## Supported Products

| Product | Supported |
| --- | --- |
| Ascend 950PR/Ascend 950DT | × |
| Atlas A3 training series / Atlas A3 inference series | √ |
| Atlas A2 training series / Atlas A2 inference series | √ |
| Atlas 200I/500 A2 inference products | × |
| Atlas inference series | × |
| Atlas training series | × |

The target environment is CANN 9.1.0 on Atlas A2 / A3, using the `dav-2201` architecture. Validate device results for the direct-launch implementation by rerunning the device tests.

## Function

Applies the 24-round Keccak-f[1600] permutation to batches of 1600-bit states. Message padding, absorption, and output expansion are not included.

## Parameters

| Parameter | Description |
| --- | --- |
| input | Contiguous UINT32 device buffer with layout `[50, B]` |
| output | UINT32 device buffer with the same layout |
| batch | Number of states, `1 <= B <= 8192` |
| stream | ACL stream on the current device; a null pointer selects the default stream |

Each state contains 25 lanes of 64 bits. The low and high 32 bits of lane `lane` occupy rows `2*lane` and `2*lane+1`, respectively; values within each row are contiguous along the batch dimension.

## Constraints

- Input and output buffers must not overlap. The caller must provide sufficient device memory.
- The `DataCopyPad` fix for unaligned tails is retained. Device tests cover boundary batch sizes including 1, 7, 8, 9, 255, 256, 257, and 8191.
- The interface launches the kernel on the specified stream and synchronizes it. Computation is complete on successful return; the return value is an ACL error code.

## Invocation

Launch the kernel directly through [CannCryptoKeccakF1600](docs/kernelLaunch_en.md). See [test_keccak_f1600_tensor.cpp](examples/test_keccak_f1600_tensor.cpp) for an example.

The build produces `libcann_crypto_keccak.so`. See [Build and Test](docs/build_en.md).

## Directory Structure

- `op_host/`: Host implementation of the public API, parameter validation, core scheduling, and runtime helpers shared by Keccak and SHAKE.
- `op_kernel/`: Ascend C permutation implementation, Kernel entry points, and the `.asc` launch wrapper; the existing implementation remains split across multiple files.
- `tests/ut/op_host/` and `tests/ut/op_kernel/`: Host and Kernel unit tests; `tests/st/`: end-to-end device tests; `tests/assets/`: reference implementations and test data.
- `examples/` and `docs/`: invocation examples and component documentation.

The public header is `include/cann_crypto/keccak.h` in the repository. Separating Host and Kernel directories preserves the direct-launch API and does not introduce ACLNN registration.
