# AICE: An Arithmetic-Oriented Stream Cipher for Heterogeneous Computing

English | [简体中文](README.md)

## Supported products

| Product | Build target | Architecture |
| --- | --- | --- |
| Ascend 910B | ascend910b | DAV2201 |
| Ascend 950 | ascend950 | DAV3510 |

## Function

Encrypts and decrypts byte data with a 256-bit key and a 96-bit IV.
Supports parallel lanes and in-place operation.
See the [algorithm and implementation specification](docs/algorithm.md).

## Parameters

`CannCryptoAiceEncrypt` and `CannCryptoAiceDecrypt` take the same parameters.

| Parameter | Description |
| --- | --- |
| input | Input device buffer |
| inputBytes | Input length in bytes |
| output | Output device buffer |
| outputCapacity | Output capacity in bytes, at least inputBytes |
| key | 32-byte key in device memory |
| iv | 12-byte IV in device memory |
| batchSize | Parallel lanes per block: 128, 256, or 512 |
| dim | Number of blocks in the kernel launch |
| stream | ACL stream on the current device; NULL selects the default stream |

## Constraints

- `inputBytes <= UINT32_MAX`; `dim > 0` and `batchSize * dim <= UINT32_MAX`. Choose a dim supported by the device.
- Exact in-place operation is supported. Partial input/output overlap and output overlap with key or IV return a parameter error.
- Buffers and stream belong to the current ACL device. The caller manages ACL setup and memory.
- Decryption uses the same key, IV, batchSize, dim, and data length as encryption. Use a distinct IV for each encryption under a key.
- Zero-length calls accept NULL data pointers. Calls synchronize the specified stream and return an ACL error code.

## Usage

The public interface is declared in [aice.h](../../../../include/cann_crypto/aice.h).
The build produces `libcann_crypto_aice.so`.
See [aice_example.cpp](examples/aice_example.cpp) for an encryption and decryption example.

[Build, test, and benchmark](docs/build.md) · [Test coverage](tests/README.md)
