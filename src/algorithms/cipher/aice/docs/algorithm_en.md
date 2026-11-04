# AICE Algorithm and DAV2201 Implementation

English | [简体中文](algorithm.md)

## Algorithm Overview

AICE targets parallel computing on NPU processors and organizes the stream cipher computation with 16-bit modular arithmetic. Multiple independent lanes can be executed in parallel through SIMD/SIMT.

Reference: [AICE: An Arithmetic-Oriented Stream Cipher for Heterogeneous Computing](https://eprint.iacr.org/2026/1181).

## Implementation Specification

The LocalTensor-based implementation lives in [device_dav2201.h](../op_kernel/device_dav2201.h).
It runs `AiceDevice<BatchSize>` on the AIV, with bytes composing 16-bit words in little-endian order.

| Item | Specification |
| --- | --- |
| Key | 256 bits, 16 16-bit words |
| External IV | 96 bits, 6 16-bit words |
| Lane initial value | IV96 concatenated with a 32-bit global lane index |
| Core state | 37 16-bit words per lane |
| Output memory | 32 16-bit words per lane |
| batchSize | 128, 256, or 512, a compile-time template parameter |
| tile | `32 × batchSize` 16-bit words, i.e. `64 × batchSize` bytes |
| Input length | 0 to UINT32_MAX bytes, odd byte tails supported |
| Output length | Same as the input length |

### Data Layout and Multi-Block Partitioning

Each tile uses an SoA layout `[32][batchSize]`: lanes at the same word position are stored contiguously.
The byte offset of word w in tile t for lane l is:

```text
offset = t * (64 * batchSize) + 2 * (w * batchSize + l)
```

The host launches the kernel once with dim blocks. Let T be the total number of tiles; for block i:

```text
T         = ceil(inputBytes / (64 * batchSize))
quotient  = T / dim
remainder = T % dim
count     = quotient + (i < remainder)
start     = i * quotient + min(i, remainder)
```

Each block processes count consecutive tiles starting at start; a count of 0 returns immediately.
batchSize determines the lane layout, dim determines the partitioning and lane indices, and
encryption and decryption use the same parameters and data length. With block index i and
in-block lane index l, the global lane index is `i * batchSize + l`, with the initial value
`IV96 || LE32(i * batchSize + l)`. Consecutive tiles of the same block share state, and every
public API call re-initializes it. A trailing odd byte is computed as a 16-bit word zero-padded
in the high byte, and only the valid byte is written back.

### UB Usage

The following table lists the explicit buffer allocation per AIV in KB, where 1 KB = 1024 bytes.

| Buffer | DAV2201 batch=128 | DAV2201 batch=256 | DAV2201 batch=512 | DAV3510 batch=128 | DAV3510 batch=256 | DAV3510 batch=512 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| inputQueue_ | 8 | 16 | 32 | 8 | 16 | 32 |
| outputQueue_ | 8 | 16 | 32 | 8 | 16 | 32 |
| keyQueue_ + ivQueue_ | 0.0625 | 0.0625 | 0.0625 | 0.0625 | 0.0625 | 0.0625 |
| tmpBuffer_ | 4 | 8 | 16 | 5.5 | 11 | 22 |
| calcBuffer_ | 9.25 | 18.5 | 37 | 18.5 | 37 | 74 |
| stateBuffer_ | 18.5 | 37 | 74 | 18.5 | 37 | 74 |
| Total | 47.8125 | 95.5625 | 191.0625 | 58.5625 | 117.0625 | 234.0625 |
