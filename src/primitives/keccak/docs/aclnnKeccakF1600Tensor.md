# aclnnKeccakF1600Tensor

头文件：`aclnn_keccak_f1600_tensor.h`，由标准算子构建生成。

```cpp
aclnnStatus aclnnKeccakF1600TensorGetWorkspaceSize(
    const aclTensor* x, const aclTensor* y,
    uint64_t* workspaceSize, aclOpExecutor** executor);

aclnnStatus aclnnKeccakF1600Tensor(
    void* workspace, uint64_t workspaceSize,
    aclOpExecutor* executor, aclrtStream stream);
```

第一阶段校验张量并生成执行器，返回所需的工作空间大小；第二阶段将计算提交到指定流。
工作空间大小为零时可传入空指针。调用方负责工作空间和张量内存的申请与释放。
第二阶段为异步执行，读取输出或释放设备资源前需同步流。

输入、输出均为连续的 `UINT32[50,B]`，布局和范围见[算子说明](../README.md)。
两个接口均以返回值 0 表示成功，非零时停止后续操作并检查 CANN 日志。

这些接口为算子级 ACLNN 调用入口，不属于 `include/cann_crypto` 的公共 C ABI。
