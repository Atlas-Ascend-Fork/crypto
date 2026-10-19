# 基准测试

简体中文 | [English](README_en.md)

必须覆盖的维度

- **batch 规模扫描**：1 / 64 / 1K / 64K等。
- **消息长度扫描**：短消息与长消息的结论往往相反。
- **每个 SoC × 每条分发路径**，不能只测默认路径。

## 组织方式

- `micro/`：单个算子或原语
- `end_to_end/`：完整场景

CI 范围以[仓内 PR 流水线配置](../.gitcode/workflows/crypto-actions.yml)为准。当前配置显式列有代码检查、x86 编译、Markdown 检查和 op_host 单元测试，没有基准性能回归任务或每日定时触发。全矩阵及性能回归的调度需另行配置。
