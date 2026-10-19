# 文档索引

简体中文 | [English](README_en.md)

| 文档                | 说明                             | 状态                 |
| ------------------- | -------------------------------- | -------------------- |
| `architecture.md` | 分层架构、API 形态与仓库组织方式 | SIG 评审中，暂未合入 |

- 与代码强关联的说明（算法接口、样例）直接放在对应代码目录下。

## 组件文档

| 组件 | 使用与接口说明 | 测试与样例 |
| --- | --- | --- |
| AES | [组件介绍（英文）](../src/primitives/AES/README.md)、[CTR](../src/primitives/AES/csrc/ctr/dav-2201/README.md)、[GCM](../src/primitives/AES/csrc/gcm/dav-2201/README.md) | [调用样例](../src/primitives/AES/examples/README.md) |
| Keccak | [组件介绍](../src/primitives/keccak/README.md)、[编译与运行](../src/primitives/keccak/docs/build.md)、[Kernel 直调接口](../src/primitives/keccak/docs/kernelLaunch.md) | [测试](../src/primitives/keccak/tests/README.md) |
| SHAKE | [组件介绍](../src/algorithms/hash/shake/README.md)、[编译与运行](../src/algorithms/hash/shake/docs/build.md)、[Kernel 直调接口](../src/algorithms/hash/shake/docs/kernelLaunch.md) | [测试](../src/algorithms/hash/shake/tests/README.md) |

## 仓库目录说明

- 接口与分层：[公共接口](../include/cann_crypto/README.md)、[C ABI 实现](../src/api/README.md)、[运行时](../src/core/README.md)、[数学引擎](../src/math/README.md)、[密码原语](../src/primitives/README.md)、[算法服务](../src/algorithms/README.md)。
- 开发与验证：[测试](../tests/README.md)、[基准测试](../benchmarks/README.md)、[参考实现](../reference/README.md)、[样例](../examples/README.md)、[辅助脚本](../scripts/README.md)。
