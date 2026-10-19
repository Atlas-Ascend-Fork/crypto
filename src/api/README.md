# api — 对外 C ABI 的实现

简体中文 | [English](README_en.md)

实现 `include/cann_crypto/` 中声明的接口。

职责：

- ABI 边界校验：空指针、`structSize` / `version`、句柄合法性、枚举越界、输入输出缓冲区重叠。
- 句柄与 plan 的生命周期管理。
- 按算法枚举分发到 `../algorithms/`。

算法特有的参数校验（密钥、nonce、tag 长度等）属于各算法的 `op_host/`，不在此处重复。
