# aclnnShake128Tensor

头文件：`aclnn_shake128_tensor.h`，由算子工程构建生成。

[查看算子定义](../op_host/shake128_tensor_def.cpp) · [查看 Kernel 源码](../op_kernel/shake128_tensor.cpp)

## 产品支持情况

| 产品 | 是否支持 |
| --- | --- |
| Ascend 950PR/Ascend 950DT | × |
| Atlas A3 训练系列产品/Atlas A3 推理系列产品 | √ |
| Atlas A2 训练系列产品/Atlas A2 推理系列产品 | √ |
| Atlas 200I/500 A2 推理产品 | × |
| Atlas 推理系列产品 | × |
| Atlas 训练系列产品 | × |

Atlas A2 的编译目标为 `ascend910b`，Atlas A3 的编译目标为 `ascend910_93`。设备验证使用 CANN 9.1.0。

## 功能说明

对 B 条等长消息计算 SHAKE128 可扩展输出函数。输入为 `[B, M]` 的 UINT8 张量，
每条消息包含 M 字节；输出为 `[B, O]` 的 UINT8 张量，每条结果包含 O 字节。
本次调用中的所有消息使用相同的输入长度 M 和输出长度 O。

$$
y_{b,:} = \operatorname{SHAKE128}(x_{b,:}, 8O), \qquad 0 \le b < B
$$

其中公式中的输出长度 8O 以位为单位，张量形状中的 O 以字节为单位。
SHAKE128 的 rate 为 168 字节。消息吸收、填充和结果挤出由算子完成；
调用方无需预先填充输入，也无需单独传入输出长度属性。输出长度由 y 的第二维指定。

## 函数原型

接口分为两段：先调用 `aclnnShake128TensorGetWorkspaceSize` 获取工作空间大小并生成执行器，
再调用 `aclnnShake128Tensor` 将计算提交到指定流。

```cpp
aclnnStatus aclnnShake128TensorGetWorkspaceSize(
    const aclTensor* x,
    const aclTensor* y,
    uint64_t* workspaceSize,
    aclOpExecutor** executor);
```

```cpp
aclnnStatus aclnnShake128Tensor(
    void* workspace,
    uint64_t workspaceSize,
    aclOpExecutor* executor,
    aclrtStream stream);
```

## aclnnShake128TensorGetWorkspaceSize

### 参数说明

| 参数名 | 输入/输出 | 描述 | 使用说明 | 数据类型 | 数据格式 | 维度（shape） | 非连续 Tensor |
| --- | --- | --- | --- | --- | --- | --- | --- |
| x | 输入 | B 条长度为 M 字节的消息。 | 非空 Tensor，连续行主序；允许 M 为 0。 | UINT8 | ND | `[B, M]` | 不支持 |
| y | 输出 | B 条长度为 O 字节的结果。 | 调用方预先创建并分配设备内存；第一维与 x 相同，第二维指定输出字节数。 | UINT8 | ND | `[B, O]` | 不支持 |
| workspaceSize | 输出 | 所需 Device 工作空间大小，单位为字节。 | 非空 `uint64_t*`，由查询接口写入。 | — | — | — | — |
| executor | 输出 | 本次计算的执行器。 | 非空 `aclOpExecutor**`，由查询接口生成，传给第二段接口。 | — | — | — | — |

### 返回值

| 返回码 | 名称 | 说明 |
| --- | --- | --- |
| 0 | ACLNN_SUCCESS | 查询成功，可继续申请工作空间并调用执行接口。 |
| 161001 | ACLNN_ERR_PARAM_NULLPTR | 参数检查发现不允许为空的指针。 |
| 161002 | ACLNN_ERR_PARAM_INVALID | 参数检查发现不支持的数据类型、格式或其他非法参数。 |

以上为常见返回码，不涵盖算子加载、Tiling 和运行时的全部错误。任一接口返回非零时，
应停止后续调用，并结合 `aclGetRecentErrMsg()` 和 CANN 日志定位。
返回码含义参见 [aclnn 接口返回码说明](https://www.hiascend.com/document/detail/zh/canncommercial/850/API/aolapi/atlasascendc_api_07_1205.html)。

## aclnnShake128Tensor

### 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| workspace | 输入 | Device 侧工作空间地址。查询返回的 `workspaceSize` 为 0 时可传入 `nullptr`；否则必须申请不少于该大小的设备内存。 |
| workspaceSize | 输入 | 工作空间大小，单位为字节。使用对应查询接口返回的值。 |
| executor | 输入 | 对应 `aclnnShake128TensorGetWorkspaceSize` 生成的执行器。不得传入其他算子的执行器。 |
| stream | 输入 | 执行计算的 ACL 流，通过 `aclrtCreateStream` 等流创建接口获得。 |

### 返回值

返回 `ACLNN_SUCCESS`（0）表示计算已成功提交，不表示 Device 计算已经完成。
返回非零时停止后续操作，并检查错误详情和 CANN 日志。
读取输出前需调用 `aclrtSynchronizeStream` 并检查同步结果。

## 约束说明

- x、y 均为连续的二维 UINT8、ND 张量，batch 维度必须相同。
- `B > 0`、`M ≥ 0`、`O > 0`；允许空消息，不支持空 batch 或零长度输出。
- `M`、`O` 不超过 `UINT32_MAX`，`B` 不超过 `UINT32_MAX - 207`。
- 输入、输出元素总数 `B × M`、`B × O` 均不超过 `INT64_MAX`，并受可用设备内存限制。
- 输入缓冲区至少包含 `B × M` 字节，输出缓冲区至少包含 `B × O` 字节。M 为 0 时仍需创建有效的输入 Tensor。
- 不支持同一批次中各消息分别指定输入长度或输出长度。
- 输入、输出使用独立缓冲区，不支持原地计算。
- 调用方负责创建输入、输出张量，并申请与释放张量内存及工作空间。输出张量参数虽然使用 `const aclTensor*`，其指向的设备数据仍会被写入。
- 每次执行使用对应查询生成的执行器，不跨次复用。流同步完成前，不得释放张量、底层设备缓冲区或工作空间。

## 调用示例

以下示例对两条空消息分别计算 32 字节 SHAKE128 结果，并检查结果的前 16 字节。
`example::Buffer(0)` 为零长度输入保留有效的设备地址。
示例使用的 [acl_example.h](../../../../primitives/keccak/examples/acl_example.h)
提供 ACL 初始化、流、设备缓冲区和张量的资源管理。

```cpp
#include <aclnn_shake128_tensor.h>
#include "acl_example.h"

#include <algorithm>
#include <array>
#include <iostream>

int main(int argc, char** argv)
{
    try {
        const int device = argc > 1 ? std::stoi(argv[1]) : 0;
        example::Session session(device);
        constexpr int64_t batch = 2;
        constexpr int64_t outputLength = 32;
        example::Buffer x(0);
        example::Buffer y(batch * outputLength);
        example::Tensor inputTensor(x, {batch, 0}, ACL_UINT8);
        example::Tensor outputTensor(y, {batch, outputLength}, ACL_UINT8);

        uint64_t workspaceSize = 0;
        aclOpExecutor* executor = nullptr;
        example::Check(aclnnShake128TensorGetWorkspaceSize(inputTensor.get(), outputTensor.get(),
            &workspaceSize, &executor), "workspace query");
        example::Buffer workspace(workspaceSize);
        example::Check(aclnnShake128Tensor(workspaceSize == 0 ? nullptr : workspace.data(),
            workspaceSize, executor, session.stream()), "Shake128Tensor");
        example::Check(aclrtSynchronizeStream(session.stream()), "stream synchronize");
        std::vector<uint8_t> output(y.size());
        example::Check(aclrtMemcpy(output.data(), output.size(), y.data(), y.size(),
            ACL_MEMCPY_DEVICE_TO_HOST), "copy output");

        const std::array<uint8_t, 16> expected = {
            0x7f, 0x9c, 0x2b, 0xa4, 0xe8, 0x8f, 0x82, 0x7d,
            0x61, 0x60, 0x45, 0x50, 0x76, 0x05, 0x85, 0x3e};
        for (size_t message = 0; message < static_cast<size_t>(batch); ++message) {
            if (!std::equal(expected.begin(), expected.end(), output.begin() + message * outputLength)) {
                throw std::runtime_error("SHAKE output mismatch");
            }
        }
        std::cout << "SHAKE128 example: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
```

工程中的完整调用示例同时支持 SHAKE128 和 SHAKE256，见
[test_aclnn_shake_tensor.cpp](../examples/test_aclnn_shake_tensor.cpp)。
构建、算子包路径和动态库配置见 [编译与运行](build.md)。在 SHAKE 算子目录配置环境后运行：

```bash
./build/cann-9.1.0-a3-sdk/examples/example_shake_tensor 0 128
```

[返回 SHAKE 接口列表](aclnnShakeTensor.md)。
