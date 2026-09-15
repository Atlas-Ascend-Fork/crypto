# aclnnKeccakF1600Tensor

头文件：`aclnn_keccak_f1600_tensor.h`，由算子工程构建生成。

[查看算子定义](../op_host/keccak_f1600_tensor_def.cpp) · [查看 Kernel 源码](../op_kernel/keccak_f1600_tensor.cpp)

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

对 B 个 1600 位状态分别执行 24 轮 Keccak-f[1600] 置换。该接口只执行置换，
不包含消息吸收、域分离、填充或结果挤出，不能直接作为 SHA3 或 SHAKE 哈希接口使用。

输入、输出均采用 `[50, B]` 的 UINT32 张量。每个状态包含 25 个 64 位 lane；
第 l 个 lane 的低 32 位存放在第 2l 行，高 32 位存放在第 2l+1 行。同一行内 B 个状态连续存放。
lane 的二维坐标为 (u, v) 时，编号为 l = u + 5v，其中 0 ≤ u, v < 5。

对每个状态 b，计算过程为：

$$
A_{l,b} = \operatorname{uint64}(x_{2l,b})
          \mathbin{\vert} \bigl(\operatorname{uint64}(x_{2l+1,b}) \ll 32\bigr)
$$

$$
A'_{:,b} = \operatorname{Keccak\text{-}f[1600]}(A_{:,b})
$$

$$
y_{2l,b} = A'_{l,b} \bmod 2^{32}, \qquad
y_{2l+1,b} = \left\lfloor A'_{l,b} / 2^{32} \right\rfloor
$$

## 函数原型

接口分为两段：先调用 `aclnnKeccakF1600TensorGetWorkspaceSize` 获取工作空间大小并生成执行器，
再调用 `aclnnKeccakF1600Tensor` 将计算提交到指定流。

```cpp
aclnnStatus aclnnKeccakF1600TensorGetWorkspaceSize(
    const aclTensor* x,
    const aclTensor* y,
    uint64_t* workspaceSize,
    aclOpExecutor** executor);
```

```cpp
aclnnStatus aclnnKeccakF1600Tensor(
    void* workspace,
    uint64_t workspaceSize,
    aclOpExecutor* executor,
    aclrtStream stream);
```

## aclnnKeccakF1600TensorGetWorkspaceSize

### 参数说明

| 参数名 | 输入/输出 | 描述 | 使用说明 | 数据类型 | 数据格式 | 维度（shape） | 非连续 Tensor |
| --- | --- | --- | --- | --- | --- | --- | --- |
| x | 输入 | 待置换的批量状态。 | 非空 Tensor，lane 的低、高 32 位按功能说明打包。 | UINT32 | ND | `[50, B]` | 不支持 |
| y | 输出 | 置换后的批量状态。 | 调用方预先创建并分配设备内存，类型、格式和形状与 x 一致。 | UINT32 | ND | `[50, B]` | 不支持 |
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

## aclnnKeccakF1600Tensor

### 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| workspace | 输入 | Device 侧工作空间地址。查询返回的 `workspaceSize` 为 0 时可传入 `nullptr`；否则必须申请不少于该大小的设备内存。 |
| workspaceSize | 输入 | 工作空间大小，单位为字节。使用对应查询接口返回的值。 |
| executor | 输入 | 对应 `aclnnKeccakF1600TensorGetWorkspaceSize` 生成的执行器。不得传入其他算子的执行器。 |
| stream | 输入 | 执行计算的 ACL 流，通过 `aclrtCreateStream` 等流创建接口获得。 |

### 返回值

返回 `ACLNN_SUCCESS`（0）表示计算已成功提交，不表示 Device 计算已经完成。
返回非零时停止后续操作，并检查错误详情和 CANN 日志。
读取输出前需调用 `aclrtSynchronizeStream` 并检查同步结果。

## 约束说明

- x、y 均为连续的二维 UINT32、ND 张量，形状必须相同，第一维必须为 50。
- batch 范围为 `1 ≤ B ≤ 8192`，不支持空 batch。
- 输入、输出设备缓冲区各至少包含 `50 × B × sizeof(uint32_t)` 字节。
- 输入、输出使用独立缓冲区，不支持原地计算。
- 调用方负责创建输入、输出张量，并申请与释放张量内存及工作空间。输出张量参数虽然使用 `const aclTensor*`，其指向的设备数据仍会被写入。
- 每次执行使用对应查询生成的执行器，不跨次复用。流同步完成前，不得释放张量、底层设备缓冲区或工作空间。

## 调用示例

以下示例创建 64 个全零状态，执行置换后检查每个状态的第一个 lane。
`acl_example.h` 提供 ACL 初始化、流、设备缓冲区和张量的资源管理；
完整源码见 [test_aclnn_keccak_f1600_tensor.cpp](../examples/test_aclnn_keccak_f1600_tensor.cpp)，
辅助代码见 [acl_example.h](../examples/acl_example.h)。

```cpp
#include <aclnn_keccak_f1600_tensor.h>
#include "acl_example.h"

#include <iostream>

int main(int argc, char** argv)
{
    try {
        const int device = argc > 1 ? std::stoi(argv[1]) : 0;
        example::Session session(device);
        constexpr int64_t batch = 64;
        std::vector<uint32_t> input(50 * batch, 0U);
        std::vector<uint32_t> output(input.size());
        example::Buffer x(input.size() * sizeof(uint32_t));
        example::Buffer y(output.size() * sizeof(uint32_t));
        example::Check(aclrtMemcpy(x.data(), x.size(), input.data(), x.size(), ACL_MEMCPY_HOST_TO_DEVICE), "copy input");
        example::Tensor inputTensor(x, {50, batch}, ACL_UINT32);
        example::Tensor outputTensor(y, {50, batch}, ACL_UINT32);

        uint64_t workspaceSize = 0;
        aclOpExecutor* executor = nullptr;
        example::Check(aclnnKeccakF1600TensorGetWorkspaceSize(inputTensor.get(), outputTensor.get(),
            &workspaceSize, &executor), "workspace query");
        example::Buffer workspace(workspaceSize);
        example::Check(aclnnKeccakF1600Tensor(workspaceSize == 0 ? nullptr : workspace.data(),
            workspaceSize, executor, session.stream()), "KeccakF1600Tensor");
        example::Check(aclrtSynchronizeStream(session.stream()), "stream synchronize");
        example::Check(aclrtMemcpy(output.data(), y.size(), y.data(), y.size(), ACL_MEMCPY_DEVICE_TO_HOST), "copy output");

        // 全零初态经 24 轮置换后，第一个 lane 为 0xf1258f7940e1dde7。
        for (size_t message = 0; message < static_cast<size_t>(batch); ++message) {
            if (output[message] != 0x40e1dde7U || output[batch + message] != 0xf1258f79U) {
                throw std::runtime_error("Keccak output mismatch");
            }
        }
        std::cout << "KeccakF1600Tensor example: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
```

构建、算子包路径和动态库配置见 [编译与运行](build.md)。在 Keccak 算子目录配置环境后运行：

```bash
./build/cann-9.1.0-a3-sdk/examples/example_keccak_f1600_tensor 0
```
