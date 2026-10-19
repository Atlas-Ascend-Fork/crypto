# Shake128Tensor / Shake256Tensor

## 产品支持情况

| 产品 | 是否支持 |
| --- | --- |
| Ascend 950PR/Ascend 950DT | × |
| Atlas A3 训练系列产品/Atlas A3 推理系列产品 | √ |
| Atlas A2 训练系列产品/Atlas A2 推理系列产品 | √ |
| Atlas 200I/500 A2 推理产品 | × |
| Atlas 推理系列产品 | × |
| Atlas 训练系列产品 | × |

目标环境为 CANN 9.1.0、Atlas A2 / A3，使用 `dav-2201` 架构。直调版本的设备结果以重新执行设备测试为准。

## 功能说明

对批量等长消息计算 SHAKE128 或 SHAKE256，输出长度由调用方指定。
SHAKE128 的 rate 为 168 字节，SHAKE256 的 rate 为 136 字节；两个算法共用一个工程。

## 参数说明

| 参数 | 描述 |
| --- | --- |
| input | 连续 UINT8 设备缓冲区，布局为 `[B, M]`；M=0 时可以为空指针 |
| output | 连续 UINT8 设备缓冲区，布局为 `[B, O]` |
| batch | 消息数量 B |
| inputLength | 每条消息的输入长度 M，单位为字节 |
| outputLength | 每条消息的输出长度 O，单位为字节 |
| stream | 当前设备的 ACL stream；空指针表示默认 stream |

## 约束说明

- 输入、输出缓冲区不得重叠，调用方负责提供足够的设备内存。
- `B > 0`、`M >= 0`、`O > 0`；允许空消息。
- M、O 不超过 `UINT32_MAX`；B 不超过 `UINT32_MAX - 207`。
- 输入、输出字节总数均不超过 `INT64_MAX`。
- 接口在指定 stream 上启动 kernel 并同步，成功返回时计算已经完成；返回值为 ACL 错误码。

## 调用说明

使用 [CannCryptoShake128 / CannCryptoShake256](docs/kernelLaunch.md)，示例见 [test_shake_tensor.cpp](examples/test_shake_tensor.cpp)。

构建生成 `libcann_crypto_shake.so`，复用 Keccak 的向量置换实现和构建辅助函数。
两个设备库分别构建，不再生成三个算子的集成 vendor 包。参见[构建与测试](docs/build.md)。
