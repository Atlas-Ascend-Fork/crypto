# CANN Crypto

面向昇腾 NPU 的密码计算库，由 CANN 社区 [crypto SIG](https://gitcode.com/cann/community/tree/master/CANN/sigs/crypto) 维护。

## 概述

CANN Crypto 为昇腾生态提供统一的密码算子与算法实现。

- 对外提供稳定的 C ABI；Host 侧使用 C++17；Device Kernel 使用 Ascend C。
- 覆盖国密（SM2、SM3、SM4、SM9、ZUC）与国际主流算法（AES、ChaCha20、SHA-2/3、X25519、Ed25519、ML-KEM、ML-DSA）。
- 以批处理与设备驻留数据为核心场景。

## 目录结构

```text
crypto/
├── include/
│   └── cann_crypto/     # 对外稳定 C ABI，唯一对外承诺兼容性的接口边界
├── src/
│   ├── api/             # C ABI 实现：边界校验、句柄与 plan 生命周期、算法分发
│   ├── core/            # L0 运行时：handle / stream / workspace / 分发 / 密钥管理 / 资源配额
│   ├── math/            # L1 数学引擎：大数与模运算、NTT/RNS、GF(2^k)、位平面转置、Cube 映射
│   ├── primitives/      # L2 密码原语：AES、SM4、Keccak-f、ChaCha、GHASH、Poly1305、EC 点运算
│   └── algorithms/      # L3 算法服务：digest / aead / mac / kdf / signature / key_exchange / kem / drbg
├── reference/           # 纯 C 参考实现，零 CANN 依赖，差分测试的正确性基准
├── tests/
│   ├── host/            # 无需昇腾设备即可运行：KAT、差分、ABI、负面用例
│   └── device/          # 需要昇腾设备：SoC × 分发路径矩阵、性能回归
├── benchmarks/          # 基准测试，device-resident 与 end-to-end 必须成对报告
├── examples/            # 最小可运行样例
├── scripts/             # 构建、测试向量获取、分层依赖检查等辅助脚本
├── docs/                # 设计与接口文档
└── README.md
```

分层依赖**只能向下**：`algorithms/` 与 `primitives/` 不实现数学运算，需要时下沉到 `math/`；算法族之间不互相依赖。各目录下的 `README.md` 说明该层的职责与约束。

## 编译安装

待补充。

## 本地验证

待补充。

## 参与贡献

欢迎通过 Issue、PR、SIG 例会等方式参与。参与前请先阅读：

- [用户行为准则](https://gitcode.com/cann/community/blob/master/contributor/code-of-conduct.md)
- [CLA 签署指南](https://gitcode.com/cann/infrastructure/blob/main/docs/cla/cla使用指南.md)（提交前必须完成签署，且 commit 邮箱需与签署邮箱一致）
- [PR 操作指南](https://gitcode.com/cann/community/blob/master/contributor/pull_request_operation.md)
- [C++ 编程规范](<https://gitcode.com/cann/community/blob/master/contributor/coding-standards/C++%20Coding%20standards.md>)与[安全编码规范](https://gitcode.com/cann/community/blob/master/contributor/coding-standards/SecureCoding.md)

## 许可证

计划采用 ？ 。
