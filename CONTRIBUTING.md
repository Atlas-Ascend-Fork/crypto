# 贡献指南

简体中文 | [English](docs/en/CONTRIBUTING.md)

本项目欢迎广大开发者体验并参与贡献。在参与社区贡献之前，请参见 [cann-community](https://gitcode.com/cann/community) 了解行为准则，完成 CLA 协议签署，并熟悉源码仓的贡献流程。

> 首次使用 GitCode 贡献时，Fork、克隆、SSH 公钥和创建 PR 的图文操作请参阅 [GitCode 工作流说明](https://gitcode.com/cann/community/blob/master/contributor/gitcode-workflow.md)。crypto 项目的具体要求见本文档。

## 准备工作

1. **签署 CLA。** 未签署或 commit 邮箱与签署邮箱不一致时，机器人会添加 `cann-cla/no` 标签，PR 无法合入。详见 [CLA 使用指南](https://gitcode.com/cann/infrastructure/blob/master/docs/cla/cla%E4%BD%BF%E7%94%A8%E6%8C%87%E5%8D%97.md)。
2. **按 PR 模板填写信息**，说明本次修改的背景、目的与方案。
3. **涉及新增算法、新增对外接口或调整目录分层时，请先通过 Issue 讨论方案**，避免代码完成后因方案分歧被拒绝合入。若不确定是否属于「简单 bug 修复」，建议先提 Issue。
4. **如使用生成式 AI 工具辅助开发，须如实声明。** PR 模板中需填写 Agent 平台及版本、模型及版本、Prompt 摘要，要求详见 [AI 编程助手使用策略](https://gitcode.com/cann/infrastructure/blob/master/docs/strategy/ai-coding-assistants.md)。声明不完整会被添加 `ai-compliance-failed` 标签。

## 贡献场景

- [贡献新算法或原语](#一贡献新算法或原语)
- [Bug 修复](#二bug-修复)
- [性能优化](#三性能优化)
- [文档纠错](#四文档纠错)
- [帮助解决他人 Issue](#五帮助解决他人-issue)

---

## 一、贡献新算法或原语

### 1. 确认代码放置层次

本项目按依赖层次组织，提交前请先确认新增代码属于哪一层：

| 目录 | 内容 | 典型贡献 |
| --- | --- | --- |
| `src/math/` | 数学引擎：大数与模运算、NTT/RNS、有限域 | 被多个算法族共享的数学实现 |
| `src/primitives/` | 密码原语：AES、SM4、Keccak-f、ChaCha 等 | 算法内核，本身不构成完整安全服务 |
| `src/algorithms/` | 算法服务：hash、aead、signature、kem 等 | 语义完整、可直接对外提供的密码服务 |

依赖方向只能向下。`src/algorithms/` 与 `src/primitives/` 不自行实现数学运算，需要时下沉到 `src/math/`；算法族之间不互相依赖。

不确定放哪一层时，请在 Issue 中说明，由 SIG 成员确认后再开始开发。

### 2. 创建 Issue 需求

新建 `Requirement|需求建议` 类 Issue，一般需包含：

- **背景信息**：该算法或原语解决什么问题
- **价值与作用**：适用场景，以及相对现有实现的意义
- **设计方案**：接口形态、依赖的下层组件、并行化思路

具体操作请参阅 [Issue 操作指南](https://gitcode.com/cann/community/blob/master/contributor/issue-operation.md)。

### 3. 需求评审

创建需求类 Issue 后，申报 SIG 议题并参加 crypto SIG 例会评审。

- [会议议题与纪要](https://etherpad-cann.meeting.osinfra.cn/p/sig-crypto)
- [会议链接](https://meeting.osinfra.cn/cann/)
- 例会时间：每月第一周周四 16:00–17:30（北京时间）

需求紧急时可申请临时 SIG 会议：先在议题页填写申请，再发邮件给 [maintainer](https://gitcode.com/cann/community/blob/master/CANN/sigs/crypto/README.md)，主题注明「申请临时 SIG 议题」及议题内容。

### 4. PR 提交

#### 交付件

```text
${component}                          # 组件名，如 chacha20_poly1305
├── ${component}_internal.h           # 中性类型的内部接口
├── ${component}_launch.cpp           # kernel 组合与变体选择
├── op_host/                          # 算子定义、infershape、tiling
├── op_kernel/                        # Ascend C 实现
├── tests/                            # 单元测试与参考实现比对
├── docs/                             # 接口说明与编译说明
├── examples/                         # 调用样例（可选）
├── CMakeLists.txt
└── README.md                         # 必选
```

`op_api/` 为可选。采用 opaque 密钥、变长批处理或逐条状态返回的算法，Tensor 语义不自然，可不提供 aclnn 直调接口；目录缺省即表示暂不支持。

#### 交付要求

| 类型 | 要求 |
| --- | --- |
| 代码 | Kernel 实现、Host 侧 tiling、单元测试 |
| 文档 | 组件 `README.md` 为必选，需注明算法状态与已实现的参数集 |
| 正确性 | 需提供已知答案测试（KAT），并与独立参考实现做差分测试 |
| 版权声明 | 新建源文件需添加版权与许可证声明头，见下 |

#### 版权声明头

新建源文件须添加：

```
Copyright (c) <年份> Huawei Technologies Co., Ltd.
Licensed under CANN Open Software License Agreement Version 2.0.
```

若文件沿用了带有其他许可证声明的模板或第三方代码，**应保留其原有声明，不得删除或替换**，并在 Issue 中说明来源，由 SIG 成员确认是否需要走三方件引入流程。

#### 合规检查

- [ ] 代码符合 [C++ 编程规范](https://gitcode.com/cann/community/blob/master/contributor/coding-standards/C++%20Coding%20standards.md)
- [ ] 代码符合[安全编码规范](https://gitcode.com/cann/community/blob/master/contributor/coding-standards/SecureCoding.md)
- [ ] 代码编译通过
- [ ] 涉及密钥的实现无与密钥相关的分支与内存寻址
- [ ] 未引入未声明的第三方代码片段，参见[片段引用指导](https://gitcode.com/cann/community/blob/master/contributor/third_party/snippet-reference-guideline.md)
- [ ] Markdown 文档语法符合规范

#### 提交规范

- 从主仓最新的 `master` 创建开发分支，推送到个人 Fork 后向 `cann/crypto:master` 提交 PR。
- PR 标题清晰，描述指明修改内容与原因，并关联对应 Issue。
- **一个 PR 只做一件事。** 不要在同一 PR 中混合功能开发与大范围目录调整。
- PR 包含多个提交时，机器人会添加 `stat/needs-squash` 标签，需压缩为单个提交后强制推送。

### 5. CI 门禁

在 PR 中评论 `/compile` 触发门禁，并根据结果修改。当前门禁包含：

- 代码编译
- 静态检查（CodeCheck、CodeCheck_style、precommit）
- 开源合规扫描（SCA、Antipoison）
- PR 检查（空文件、二进制文件等）

门禁执行时间限制为 15 分钟。因此 `tests/host/` 下无需昇腾设备的用例放入门禁，`tests/device/` 下需要真实设备的完整矩阵与性能回归放入每日构建。

### 6. Committer 检视

门禁通过后，可在 PR 评论区 `@` 相关 [Committer](https://gitcode.com/cann/community/blob/master/CANN/sigs/crypto/README.md) 提醒检视，并及时响应检视意见。

涉及 `src/math/` 的改动会被多个算法族复用，接口一经稳定难以调整，需要由负责该层的 Maintainer 参与评审。

### 7. Maintainer 合入

PR 集齐 `cann-cla/yes`、`lgtm`、`approved`、`ci-pipeline-passed` 后由机器人自动合入。

注意：**PR 作者不能批准自己的 PR**；新增提交会使早于该提交的评审失效，需请评审人重新评论。

---

## 二、Bug 修复

1. 在 Issue 中描述问题现象、复现步骤与运行环境。
2. 简单修复可直接提交 PR 并关联 Issue；涉及接口或行为变更的，先在 Issue 中讨论方案。
3. **修复正确性问题时，须补充能够复现该问题的测试用例**，避免回归。

## 三、性能优化

1. 在 Issue 中说明优化点、预期收益与验证方法。
2. PR 中需提供优化前后的对比数据，并注明测试环境、SoC 型号、批量规模与消息长度。
3. **性能数据须区分「数据已在设备」与「包含主机与设备间搬运」两种口径**，两者结论往往不同。
4. CPU 基线需注明型号、线程数与编译选项，且应开启该平台可用的密码扩展指令。

## 四、文档纠错

发现文档错误、表述不清或链接失效，可直接提交 PR 修改，无需先创建 Issue。

## 五、帮助解决他人 Issue

欢迎在 [Issue 列表](https://gitcode.com/cann/crypto/issues)中解答他人问题。解答涉及代码修改时，可直接提交 PR 并关联该 Issue。

---

## 附录

### 相关链接

| 内容 | 链接 |
| --- | --- |
| GitCode 工作流图文说明 | [gitcode-workflow.md](https://gitcode.com/cann/community/blob/master/contributor/gitcode-workflow.md) |
| 行为准则 | [code-of-conduct.md](https://gitcode.com/cann/community/blob/master/contributor/code-of-conduct.md) |
| CLA 使用指南 | [cla 使用指南](https://gitcode.com/cann/infrastructure/blob/master/docs/cla/cla%E4%BD%BF%E7%94%A8%E6%8C%87%E5%8D%97.md) |
| PR 操作指南 | [pull_request_operation.md](https://gitcode.com/cann/community/blob/master/contributor/pull_request_operation.md) |
| 机器人命令说明 | [robot 使用指南](https://gitcode.com/cann/infrastructure/blob/main/docs/robot/robot%E4%BD%BF%E7%94%A8%E6%8C%87%E5%8D%97.md) |
| AI 编程助手使用策略 | [ai-coding-assistants.md](https://gitcode.com/cann/infrastructure/blob/master/docs/strategy/ai-coding-assistants.md) |
| crypto SIG 主页 | [CANN/sigs/crypto](https://gitcode.com/cann/community/tree/master/CANN/sigs/crypto) |
| 安全声明 | [SECURITY.md](SECURITY.md) |
