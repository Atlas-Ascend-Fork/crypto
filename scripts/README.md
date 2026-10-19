# 辅助脚本

简体中文 | [English](README_en.md)

用于构建、测试向量获取与代码检查等辅助流程。

- **`ci/`**：门禁流水线调用的编译脚本 `compile.sh`、UT 脚本 `ut.sh` 与 NPU 冒烟脚本 `smoke.sh`，由 SIG 自行评审；稳定后迁移到 `.gitcode/scripts/`。

规划中的脚本：

- **测试向量获取**：拉取 NIST ACVP、Wycheproof、GM/T 标准向量并校验哈希，向量文件不入库。
- **分层依赖检查**：确认 `algorithms/`、`primitives/` 没有绕过 `math/` 自行实现数学运算。
- **构建产物依赖检查**：确认核心库未链入适配层的第三方依赖。
