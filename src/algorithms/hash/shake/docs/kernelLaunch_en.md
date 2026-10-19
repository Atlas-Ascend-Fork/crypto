# CannCryptoShake128 / CannCryptoShake256

English | [简体中文](kernelLaunch.md)

## Function Prototypes

Public header: `include/cann_crypto/shake.h`.

```c
int32_t CannCryptoShake128(
    const uint8_t* input, uint8_t* output, uint64_t batch,
    uint64_t inputLength, uint64_t outputLength, void* stream);
int32_t CannCryptoShake256(
    const uint8_t* input, uint8_t* output, uint64_t batch,
    uint64_t inputLength, uint64_t outputLength, void* stream);
```

## Parameters and Constraints

`input` and `output` are contiguous UINT8 buffers on the current device, with layouts `[batch, inputLength]` and `[batch, outputLength]`, respectively.
Message lengths are measured in bytes. `input` may be a null pointer for empty messages. The output pointer must be valid, and the buffers must not overlap.

- `1 <= batch <= UINT32_MAX - 207`.
- `0 <= inputLength <= UINT32_MAX`.
- `1 <= outputLength <= UINT32_MAX`.
- The total input and output byte counts must each not exceed `INT64_MAX` and are also limited by available memory.

## Invocation and Return Value

The caller must initialize ACL, select the current device, and provide a stream belonging to that device. A null stream selects the default stream.
The interface retains the existing Host group scheduling, launches the corresponding kernel through `<<<>>>`, and synchronizes the stream.
It returns 0 on success, or an ACL error code for invalid arguments, device-query failures, or synchronization failures.

See [test_shake_tensor.cpp](../examples/test_shake_tensor.cpp) for an example and [build_en.md](build_en.md) for build instructions.
