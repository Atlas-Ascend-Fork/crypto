<div align="center">

# crypto

[![license](https://img.shields.io/badge/license-CANN_Open_2.0-blue.svg)](LICENSE)
[![contributing](https://img.shields.io/badge/贡献指南-teal)](CONTRIBUTING.md)
[![SIG](https://img.shields.io/badge/SIG-crypto-yellow)](https://gitcode.com/cann/community/blob/master/CANN/sigs/crypto/README.md)

</div>

---

## 🚀概述

crypto 是 [CANN](https://hiascend.com/software/cann)（Compute Architecture for Neural Networks）中面向密码计算的算子与算法库，为昇腾生态提供统一、可复用的密码能力。

项目聚焦两类 CPU 难以覆盖的场景：让已位于 NPU 上的数据不必回到主机即可被保护，以及把 CPU 难以承担的密码计算放到 NPU 上完成。

- 覆盖国密算法（SM2、SM3、SM4、SM9、ZUC）与国际主流算法（AES、ChaCha20、SHA-2/3、X25519、Ed25519、ML-KEM、ML-DSA）。
- 面向批处理与设备驻留数据设计，Device Kernel 使用 Ascend C 实现。
- 按数学引擎、密码原语、算法服务三层组织，下层能力可被多个算法族复用。

## 📌版本配套

本项目源码跟随 CANN 软件版本发布。关于 CANN 软件版本与本项目标签的对应关系，请参阅 [release 仓库](https://gitcode.com/cann/release-management)中的相应版本说明。

请注意，为确保源码定制开发顺利进行，请选择配套的 CANN 版本与 GitCode 标签源码，使用 master 分支可能存在版本不匹配的风险。

## 🛠️环境准备

体验本项目能力前，请先完成 NPU 驱动与 CANN 包安装，确保环境正常。部分组件依赖 torch 与 torch_npu，具体依赖以各组件 `requirements.txt` 为准。

亦可使用社区提供的 [CANNLab 在线环境](https://gitcode.com/org/cann/cannlab/environment)，其中已预置配套的 CANN 环境。

## ⬇️源码下载

```bash
# 通用命令：git clone -b ${tag_version} https://gitcode.com/cann/crypto.git
git clone https://gitcode.com/cann/crypto.git
```

> 说明：若环境中已存在配套分支源码，**可跳过本步骤**。

## 🔨编译与验证

仓库根目录的 `build.sh` 为统一的构建与测试入口。

```bash
# 编译全部组件
bash build.sh --pkg

# 编译指定组件
bash build.sh --components=primitives/AES --pkg

# 运行单元测试
bash build.sh -u --ophost      # op_host 层，无需昇腾设备
bash build.sh -u --opapi       # op_api 层，无需昇腾设备
bash build.sh -u --opkernel    # op_kernel 层，需要昇腾设备
bash build.sh -u               # 全部层次

# 查看全部选项
bash build.sh -h
```

构建脚本在 `src/` 下递归查找带有 `CMakeLists.txt` 或 `setup.py` 的组件目录，并跳过已归属上层组件的嵌套构建目录；组件可以位于不同深度，例如 `src/algorithms/hash/shake/`。新增组件无需修改构建脚本。

## 📂目录结构

```text
crypto/
├── include/cann_crypto/   # 对外稳定 C ABI，唯一承诺兼容性的接口边界
├── src/
│   ├── api/               # C ABI 实现：边界校验、句柄与 plan 生命周期、算法分发
│   ├── core/              # 运行时：handle / stream / workspace / 分发 / 密钥管理
│   ├── math/              # 数学引擎：大数与模运算、NTT/RNS、GF(2^k)、位平面转置
│   ├── primitives/        # 密码原语：AES、SM4、Keccak-f、ChaCha、GHASH、Poly1305、EC
│   └── algorithms/        # 算法服务：digest / aead / mac / kdf / signature / kem
├── reference/             # 纯 C 参考实现，零 CANN 依赖，差分测试的正确性基准
├── tests/ut/              # 仓库级用例，按 common / op_api / op_host / op_kernel 划分
├── benchmarks/            # 基准测试
├── examples/              # 调用样例
├── scripts/               # 测试向量获取、依赖检查等辅助脚本
├── docs/                  # 设计与接口文档
└── build.sh               # 构建与测试入口
```

分层依赖只能向下：`algorithms/` 与 `primitives/` 不实现数学运算，需要时下沉到 `math/`；算法族之间不互相依赖。各目录下的 `README.md` 说明该层的职责与约束。

## 📖学习教程

- [贡献指南](CONTRIBUTING.md)：贡献流程、代码放置层次、交付件要求与评审规则。
- [文档中心](docs/)：设计文档与接口说明。

## 💬相关信息

- [贡献指南](CONTRIBUTING.md)
- [安全声明](SECURITY.md)
- [许可证](LICENSE)
- [所属 SIG](https://gitcode.com/cann/community/tree/master/CANN/sigs/crypto)
- [committer 列表](https://gitcode.com/cann/community/blob/master/CANN/sigs/crypto/README.md#committer%E5%88%97%E8%A1%A8)

-----
PS：本项目功能和文档正在持续更新和完善中，欢迎您关注最新版本。

- **问题反馈**：通过 GitCode [【Issues】](https://gitcode.com/cann/crypto/issues)提交问题。
- **社区互动**：通过 crypto SIG [月度例会](https://gitcode.com/cann/community/tree/master/CANN/sigs/crypto)与[邮件列表](https://mailweb.cann.osinfra.cn/mailman3/lists/crypto.cann.osinfra.cn/)参与交流。
