# AICE: An Arithmetic-Oriented Stream Cipher for Heterogeneous Computing

简体中文 | [English](README_en.md)

## 产品支持情况

| 产品 | 编译目标 | 架构 |
| --- | --- | --- |
| Ascend 910B | ascend910b | DAV2201 |
| Ascend 950 | ascend950 | DAV3510 |

## 功能说明

使用 256 位密钥和 96 位 IV 对字节数据加密、解密，支持批量并行和原地处理。
算法与实现规格见[算法说明](docs/algorithm.md)。

## 参数说明

加密接口 `CannCryptoAiceEncrypt` 与解密接口 `CannCryptoAiceDecrypt` 使用相同参数。

| 参数 | 描述 |
| --- | --- |
| input | 输入设备缓冲区 |
| inputBytes | 输入长度，单位为字节 |
| output | 输出设备缓冲区 |
| outputCapacity | 输出容量，单位为字节，至少为 inputBytes |
| key | 32 字节密钥，存放于设备内存 |
| iv | 12 字节 IV，存放于设备内存 |
| batchSize | 每个 block 的并行 lane 数，取 128、256 或 512 |
| dim | 本次 kernel 启动的 block 数 |
| stream | 当前设备的 ACL stream；空指针表示默认 stream |

## 约束说明

- `inputBytes <= UINT32_MAX`；`dim > 0` 且 `batchSize * dim <= UINT32_MAX`，dim 应符合设备的启动范围。
- 输入和输出支持精确原地加解密；部分重叠、输出覆盖 key 或 IV 会返回参数错误。
- 缓冲区和 stream 属于当前 ACL 设备，内存分配、释放及 ACL 环境由调用方管理。
- 加解密使用相同的 key、IV、batchSize、dim 和数据长度；key、IV 组合不可重用。
- 零长度允许数据指针为空；接口同步指定 stream，返回 ACL 错误码。

## 调用说明

公开接口见 [aice.h](../../../../include/cann_crypto/aice.h)，构建生成 `libcann_crypto_aice.so`。
加解密调用示例见 [aice_example.cpp](examples/aice_example.cpp)。

[编译、测试与性能测试](docs/build.md) · [测试事项](tests/README.md)
