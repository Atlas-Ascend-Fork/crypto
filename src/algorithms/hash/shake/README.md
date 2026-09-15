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

Atlas A2 使用 `ascend910b`，Atlas A3 使用 `ascend910_93`。设备验证使用 CANN 9.1.0。

## 功能说明

- 算子功能：对批量等长消息计算 SHAKE128 或 SHAKE256，输出长度由调用方指定。
- Shake128Tensor 使用 SHAKE128，rate 为 168 字节；Shake256Tensor 使用 SHAKE256，rate 为 136 字节。

## 参数说明

| 参数名 | 输入/输出/属性 | 描述 | 数据类型 | 数据格式 |
| --- | --- | --- | --- | --- |
| x | 输入 | B 条长度为 M 字节的消息，形状为 `[B, M]` | UINT8 | ND |
| y | 输出 | B 条长度为 O 字节的结果，形状为 `[B, O]` | UINT8 | ND |

同一次调用中，所有消息使用相同的输入长度 M 和输出长度 O。

## 约束说明

- 输入、输出为连续行主序张量，缓冲区不重叠。
- `B > 0`、`M >= 0`、`O > 0`；允许空消息，不支持空 batch 或零长度输出。
- M、O 不超过 `UINT32_MAX`；B 不超过 `UINT32_MAX - 207`，以满足设备分组索引的范围要求。
- 输入、输出元素总数不超过 `INT64_MAX`，并受可用设备内存限制。
- 输出形状由调用方指定。执行完成前不得释放张量及工作空间。

## 调用说明

| 调用方式 | 样例代码 | 说明 |
| --- | --- | --- |
| aclnn接口 | [test_aclnn_shake_tensor.cpp](examples/test_aclnn_shake_tensor.cpp) | 通过 [aclnnShake128Tensor](docs/aclnnShake128Tensor.md) 接口调用 Shake128Tensor 算子。 |
| aclnn接口 | [test_aclnn_shake_tensor.cpp](examples/test_aclnn_shake_tensor.cpp) | 通过 [aclnnShake256Tensor](docs/aclnnShake256Tensor.md) 接口调用 Shake256Tensor 算子。 |
