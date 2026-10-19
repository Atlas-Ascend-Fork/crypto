# math — 数学引擎层

简体中文 | [English](README_en.md)

被所有算法族共享的数学实现。

范围：

- 大数与模运算（Montgomery、Barrett）
- NTT / INTT / RNS
- GF(2^k)
- 位平面（bit-plane）转置
- 面向 Cube 单元的限位整数卷积

约束：

- 本目录有单一 owner。
- 不依赖任何具体算法的类型。
- `primitives/` 与 `algorithms/` 不得自行实现本目录已覆盖的运算。
