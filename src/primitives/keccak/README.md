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

目标环境为 CANN 9.1.0、Atlas A2 / A3，使用 `dav-2201` 架构。直调版本的设备结果以重新执行设备测试为准。

## 功能说明

对批量 1600 位状态执行 Keccak-f[1600] 的 24 轮置换，不包含消息填充、吸收和输出扩展。

## 参数说明

| 参数 | 描述 |
| --- | --- |
| input | 连续 UINT32 设备缓冲区，布局为 `[50, B]` |
| output | 相同布局的 UINT32 设备缓冲区 |
| batch | 状态数量，`1 <= B <= 8192` |
| stream | 当前设备的 ACL stream；空指针表示默认 stream |

每个状态包含 25 个 64 位 lane。第 `lane` 个值的低、高 32 位分别放在第 `2*lane`、`2*lane+1` 行；行内沿 batch 维连续存放。

## 约束说明

- 输入、输出缓冲区不得重叠，调用方负责提供足够的设备内存。
- 保留非对齐尾块的 `DataCopyPad` 修复；设备测试包括 batch 1、7、8、9、255、256、257、8191 等边界。
- 接口在指定 stream 上启动 kernel 并同步，成功返回时计算已经完成；返回值为 ACL 错误码。

## 调用说明

通过 [CannCryptoKeccakF1600](docs/kernelLaunch.md) 直接启动 kernel，示例见 [test_keccak_f1600_tensor.cpp](examples/test_keccak_f1600_tensor.cpp)。

构建生成 `libcann_crypto_keccak.so`，参见[构建与测试](docs/build.md)。
