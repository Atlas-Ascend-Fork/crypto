# SHAKE ACLNN 接口

SHAKE128 和 SHAKE256 共用一个 SHAKE 算子工程，分别提供以下两组接口。

| 算子 | 头文件 | 接口文档 |
| --- | --- | --- |
| Shake128Tensor | `aclnn_shake128_tensor.h` | [aclnnShake128Tensor](aclnnShake128Tensor.md) |
| Shake256Tensor | `aclnn_shake256_tensor.h` | [aclnnShake256Tensor](aclnnShake256Tensor.md) |

每组接口均包含工作空间查询和异步执行两个阶段。输入为连续 UINT8 `[B, M]` 张量，
输出为连续 UINT8 `[B, O]` 张量。输出长度由调用方创建的输出张量形状指定。

产品支持、参数、返回值、约束和调用示例见各接口文档。
工程构建和运行环境配置见 [编译与运行](build.md)。
