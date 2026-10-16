# 测试

目录结构与 `ops-math`、`ops-nn`、`ops-transformer` 保持一致：仓库级用例放在
`ut/` 下，按算子代码层划分；组件自带用例放在各组件的 `tests/ut/` 下，采用
相同的层次划分。

| 目录 | 内容 | 是否需要昇腾设备 |
| --- | --- | --- |
| `ut/common/` | 跨算法的公共用例：KAT 向量比对、与 `reference/` 的差分测试、ABI 兼容性检查 | 否 |
| `ut/op_api/` | aclnn 接口层 | 否 |
| `ut/op_host/` | tiling、infershape 等 Host 侧逻辑 | 否 |
| `ut/op_kernel/` | Ascend C 核函数 | 是 |

运行方式：

```bash
bash build.sh -u                # 全部
bash build.sh -u --ophost       # 仅 op_host 层
bash build.sh -u --opkernel     # 仅 op_kernel 层
```

门禁单次执行限时 15 分钟，因此只运行不需要设备的层次；`op_kernel` 层的完整
矩阵与性能回归放入每日构建。

NIST ACVP、Wycheproof 与 GM/T 标准向量的获取及哈希校验脚本仍在规划中，见[辅助脚本说明](../scripts/README.md)。当前已实现的组件测试使用随源码提供的参考实现与已知答案，例如[Keccak 测试](../src/primitives/keccak/tests/README.md)和[SHAKE 测试](../src/algorithms/hash/shake/tests/README.md)。
