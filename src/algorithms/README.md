# algorithms — 算法服务层

语义完整的密码服务，按算法族分目录：`digest` / `aead` / `mac` / `kdf` / `signature` / `key_exchange` / `kem`。

每个算法目录的组织方式与 CANN 算子仓保持一致：

```text
<alg>/
├── <alg>_internal.h    # 中性类型的内部接口，原生 API 与 aclnn 入口共用
├── <alg>_launch.cpp    # kernel 组合与变体选择
├── op_api/             # aclnn 两段式入口（可选；缺省表示暂不支持 aclnn 直调）
├── op_host/            # 算子定义、infershape、tiling
├── op_kernel/          # Ascend C 实现
├── tests/
└── CMakeLists.txt
```

约束：

- 只做组合、参数校验与调度，不实现数学运算。
- 算法族之间不得互相依赖；需要共用能力时下沉到 `../primitives/` 或 `../math/`。
- `op_host/` 的 tiling 接口只接受中性标量类型，以便原生 API 与 aclnn 入口共用同一份实现。
