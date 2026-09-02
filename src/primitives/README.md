# primitives — 密码原语层

算法内核实现：AES、SM4、Keccak-f、ChaCha、GHASH、Poly1305、EC 点运算等。

约束：

- 这些原语单独使用并不构成完整的安全服务（例如 GHASH 本身不是 MAC），因此不对最终用户默认暴露。
- 不实现数学运算，需要时下沉到 `../math/`。
- 算法族之间不得通过本目录互相依赖。
