# CannCryptoShake128 / CannCryptoShake256

## 接口原型

公共头文件：`include/cann_crypto/shake.h`。

```c
int32_t CannCryptoShake128(
    const uint8_t* input, uint8_t* output, uint64_t batch,
    uint64_t inputLength, uint64_t outputLength, void* stream);
int32_t CannCryptoShake256(
    const uint8_t* input, uint8_t* output, uint64_t batch,
    uint64_t inputLength, uint64_t outputLength, void* stream);
```

## 参数与约束

input、output 是当前设备上的连续 UINT8 缓冲区，布局分别为 `[batch, inputLength]`、`[batch, outputLength]`。
每条消息长度以字节计；输入为空消息时允许 input 为空指针。输出指针必须有效，缓冲区不得重叠。

- `1 <= batch <= UINT32_MAX - 207`。
- `0 <= inputLength <= UINT32_MAX`。
- `1 <= outputLength <= UINT32_MAX`。
- 输入、输出字节总数均不超过 `INT64_MAX`，并受实际可用内存限制。

## 调用与返回

调用方先初始化 ACL、设置当前设备并提供对应 stream，空 stream 表示默认 stream。
接口沿用已有 Host 分组调度，通过 `<<<>>>` 启动对应 Kernel 并同步 stream。
成功返回 0；入参错误、设备查询失败或同步失败返回 ACL 错误码。

示例见 [test_shake_tensor.cpp](../examples/test_shake_tensor.cpp)，构建说明见 [build.md](build.md)。
