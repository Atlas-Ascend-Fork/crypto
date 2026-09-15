# KeccakF1600Tensor

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

- 算子功能：对批量 1600 位状态执行 Keccak-f[1600] 的 24 轮置换，不包含消息填充、吸收和输出扩展。

## 参数说明

| 参数名 | 输入/输出/属性 | 描述 | 数据类型 | 数据格式 |
| --- | --- | --- | --- | --- |
| x | 输入 | 初始状态，形状为 `[50, B]` | UINT32 | ND |
| y | 输出 | 置换结果，形状为 `[50, B]` | UINT32 | ND |

每个状态包含 25 个 64 位 lane。第 `lane` 个值的低、高 32 位分别放在
第 `2*lane`、`2*lane+1` 行；`lane=x+5*y`，行内沿 batch 维连续存放。

## 约束说明

- 输入、输出为连续张量，形状一致，缓冲区不重叠。
- `1 <= B <= 8192`；已覆盖的设备测试 batch 为 64、256、1024、4096、8192。
- 调用方在流同步完成前保留输入、输出和工作空间。

## 调用说明

| 调用方式 | 样例代码 | 说明 |
| --- | --- | --- |
| aclnn接口 | [test_aclnn_keccak_f1600_tensor.cpp](examples/test_aclnn_keccak_f1600_tensor.cpp) | 通过 [aclnnKeccakF1600Tensor](docs/aclnnKeccakF1600Tensor.md) 接口调用 KeccakF1600Tensor 算子。 |
