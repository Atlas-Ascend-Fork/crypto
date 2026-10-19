# CannCryptoKeccakF1600

English | [简体中文](kernelLaunch.md)

## Function Prototype

Public header: `include/cann_crypto/keccak.h`.

```c
int32_t CannCryptoKeccakF1600(
    const uint32_t* input, uint32_t* output, uint64_t batch, void* stream);
```

`input` and `output` are contiguous UINT32 buffers on the current device with layout `[50, batch]`. Each buffer must contain at least `batch * 200` bytes, and the two buffers must not overlap.
`1 <= batch <= 8192`. The low and high 32 bits of each state lane occupy adjacent rows, with the batch dimension stored contiguously.

The caller must initialize ACL, select the current device, and ensure that the stream belongs to that device.
The interface creates a launch configuration using the actual AIV core count, launches the kernel directly through `<<<>>>`, and synchronizes the stream.
It returns 0 on success, or the corresponding ACL error code for invalid parameters or runtime errors. The output is ready to read on successful return.

See [test_keccak_f1600_tensor.cpp](../examples/test_keccak_f1600_tensor.cpp) for an example and [build_en.md](build_en.md) for build instructions.
