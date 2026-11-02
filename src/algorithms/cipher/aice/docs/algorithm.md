# AICE 算法与 DAV2201 实现

## 算法简介

AICE 面向 NPU 处理器的并行计算，以 16 位模算术组织流密码计算，
多个独立 lane 可通过 SIMD/SIMT 并行实现。

参考文献 [AICE: An Arithmetic-Oriented Stream Cipher for Heterogeneous Computing](https://eprint.iacr.org/2026/1181)。

## 实现规格

基于 LocalTensor 的实现位于 [device_dav2201.h](../op_kernel/device_dav2201.h)，
以 `AiceDevice<BatchSize>` 在 AIV 上执行，字节按小端序组成 16 位字。

| 项目 | 规格 |
| --- | --- |
| 密钥 | 256 位，16 个 16 位字 |
| 外部 IV | 96 位，6 个 16 位字 |
| lane 初值 | IV96 与 32 位全局 lane 编号拼接 |
| 核心状态 | 每条 lane 37 个 16 位字 |
| 输出记忆 | 每条 lane 32 个 16 位字 |
| batchSize | 128、256、512，编译期模板参数 |
| tile | `32 × batchSize` 个 16 位字，即 `64 × batchSize` 字节 |
| 输入长度 | 0 至 UINT32_MAX 字节，支持奇数字节尾部 |
| 输出长度 | 与输入长度相同 |

### 数据布局与多核分块

每个 tile 使用 SoA 布局 `[32][batchSize]`：同一字位置上的各 lane 连续存放。
第 t 个 tile、第 w 个字、第 l 条 lane 的字节偏移为：

```text
offset = t * (64 * batchSize) + 2 * (w * batchSize + l)
```

Host 使用 dim 启动一次 kernel。令 T 为总 tile 数，对第 i 个 block：

```text
T         = ceil(inputBytes / (64 * batchSize))
quotient  = T / dim
remainder = T % dim
count     = quotient + (i < remainder)
start     = i * quotient + min(i, remainder)
```

该 block 连续处理从 start 开始的 count 个 tile；count 为 0 时直接返回。
batchSize 决定 lane 排布，dim 决定分块和 lane 编号，加解密使用相同参数和数据长度。
block 编号为 i、block 内 lane 编号为 l 时，全局 lane 编号为 `i * batchSize + l`，
对应初值为 `IV96 || LE32(i * batchSize + l)`。
同一 block 的连续 tile 共用状态，每次公开接口调用重新初始化。
末尾奇数字节按高字节补零的 16 位字计算，只写回有效字节。

### UB 占用

下表为每个 AIV 的显式 buffer 分配，单位为 KB，按 1 KB = 1024 字节换算。

| Buffer | DAV2201 batch=128 | DAV2201 batch=256 | DAV2201 batch=512 | DAV3510 batch=128 | DAV3510 batch=256 | DAV3510 batch=512 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| inputQueue_ | 8 | 16 | 32 | 8 | 16 | 32 |
| outputQueue_ | 8 | 16 | 32 | 8 | 16 | 32 |
| keyQueue_ + ivQueue_ | 0.0625 | 0.0625 | 0.0625 | 0.0625 | 0.0625 | 0.0625 |
| tmpBuffer_ | 4 | 8 | 16 | 5.5 | 11 | 22 |
| calcBuffer_ | 9.25 | 18.5 | 37 | 18.5 | 37 | 74 |
| stateBuffer_ | 18.5 | 37 | 74 | 18.5 | 37 | 74 |
| 合计 | 47.8125 | 95.5625 | 191.0625 | 58.5625 | 117.0625 | 234.0625 |
