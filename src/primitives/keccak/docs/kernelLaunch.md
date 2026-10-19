# CannCryptoKeccakF1600

## 接口原型

公共头文件：`include/cann_crypto/keccak.h`。

```c
int32_t CannCryptoKeccakF1600(
    const uint32_t* input, uint32_t* output, uint64_t batch, void* stream);
```

input、output 为当前设备上的连续 `[50, batch]` UINT32 缓冲区，每个缓冲区至少 `batch * 200` 字节，二者不得重叠。
`1 <= batch <= 8192`。状态 lane 的低、高 32 位位于相邻两行，batch 维连续。

调用方先初始化 ACL、设置当前设备，并保证 stream 属于该设备。
接口按实际 AIV 核数生成启动配置，通过 `<<<>>>` 直接启动 Kernel，并同步 stream。
成功返回 0，参数错误或运行时错误返回对应 ACL 错误码；成功返回后可以读取输出。

示例见 [test_keccak_f1600_tensor.cpp](../examples/test_keccak_f1600_tensor.cpp)，构建说明见 [build.md](build.md)。
