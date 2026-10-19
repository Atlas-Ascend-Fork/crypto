# Shake128Tensor / Shake256Tensor

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

Computes SHAKE128 or SHAKE256 for batches of equal-length messages, with the output length specified by the caller.
SHAKE128 has a rate of 168 bytes and SHAKE256 has a rate of 136 bytes; both algorithms share one project.

## Parameters

| Parameter | Description |
| --- | --- |
| input | Contiguous UINT8 device buffer with layout `[B, M]`; may be a null pointer when M=0 |
| output | Contiguous UINT8 device buffer with layout `[B, O]` |
| batch | Number of messages B |
| inputLength | Input length M per message, in bytes |
| outputLength | Output length O per message, in bytes |
| stream | ACL stream on the current device; a null pointer selects the default stream |

## Constraints

- Input and output buffers must not overlap. The caller must provide sufficient device memory.
- `B > 0`, `M >= 0`, and `O > 0`; empty messages are supported.
- M and O must not exceed `UINT32_MAX`; B must not exceed `UINT32_MAX - 207`.
- The total input and output byte counts must each not exceed `INT64_MAX`.
- The interface launches the kernel on the specified stream and synchronizes it. Computation is complete on successful return; the return value is an ACL error code.

## Invocation

Use [CannCryptoShake128 / CannCryptoShake256](docs/kernelLaunch_en.md). See [test_shake_tensor.cpp](examples/test_shake_tensor.cpp) for an example.

The build produces `libcann_crypto_shake.so`, reusing Keccak's vector permutation implementation and build helpers.
The two device libraries are built separately; an integrated vendor package containing all three operators is no longer generated. See [Build and Test](docs/build_en.md).
