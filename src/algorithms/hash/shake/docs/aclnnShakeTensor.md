# SHAKE ACLNN 接口

| 算子 | 生成头文件 | Workspace 查询 | 执行接口 |
| --- | --- | --- | --- |
| Shake128Tensor | aclnn_shake128_tensor.h | aclnnShake128TensorGetWorkspaceSize | aclnnShake128Tensor |
| Shake256Tensor | aclnn_shake256_tensor.h | aclnnShake256TensorGetWorkspaceSize | aclnnShake256Tensor |

两组接口使用相同的参数形式：

```cpp
aclnnStatus aclnnShake128TensorGetWorkspaceSize(
    const aclTensor* x, const aclTensor* y,
    uint64_t* workspaceSize, aclOpExecutor** executor);

aclnnStatus aclnnShake128Tensor(
    void* workspace, uint64_t workspaceSize,
    aclOpExecutor* executor, aclrtStream stream);
```

SHAKE256 将函数名中的 `128` 替换为 `256`。
输入为连续 `UINT8[B,M]`，输出为连续 `UINT8[B,O]`。
输出形状由调用方创建，不通过额外属性传递输出长度。

1. 创建输入、输出设备缓冲区及 `aclTensor`。
2. 调用 Workspace 查询接口，检查返回值并申请工作空间。
3. 调用执行接口；工作空间大小为零时传入空指针。
4. 同步流，读取输出并释放调用方持有的资源。

每次执行使用对应查询生成的执行器，不跨次复用。
返回值 0 表示成功；任一阶段返回非零时停止后续调用。

这些接口为算子级 ACLNN 调用入口，不属于 `include/cann_crypto` 的公共 C ABI。
