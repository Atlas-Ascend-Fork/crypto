# 对外公共接口

简体中文 | [English](README_en.md)

本目录是 CANN Crypto 唯一对外承诺兼容性的接口边界。

约束：

- 纯 C。不出现 C++ 类型、异常、模板，也不暴露内部结构体成员。
- 公共描述符包含 `structSize` 与 `version` 字段并预留 `reserved`，以便在不破坏 ABI 的前提下扩展。
- 不依赖 `aclTensor`。张量形态的接口由适配层提供。
