# 组件组织约定

简体中文 | [English](component_layout_en.md)

组件按职责划分 Host 与 Kernel，文件数量随实现复杂度决定。
`op_host/`、`op_kernel/` 是源码组织约定；Kernel 直调无需因此引入 ACLNN 注册。

```text
src/.../<组件>/
├── op_host/          # 校验、Tiling、调度与 Host 运行时调用
├── op_kernel/        # Kernel 入口、薄启动封装、多个实现头文件
├── tests/
│   ├── ut/op_host/
│   ├── ut/op_kernel/
│   ├── st/
│   └── assets/
├── examples/
├── docs/
├── CMakeLists.txt
├── README.md
└── README_en.md
```

只有存在相应实现时才创建目录。公共原生接口放在 `include/cann_crypto/`。
普通 Host 源码使用 `.cpp`；现有 Kernel `.cpp` 通过 CANN 构建规则编译，
新 ASC 编译单元可以使用 `.asc`。目录迁移不要求改变编译模式或合并实现文件。

SHAKE128 与 SHAKE256 是同一 `shake/` 组件中的两个公开接口，保留各自的
Kernel 入口与测试，复用海绵计算实现。NTT 的阶段、缓冲区、模运算和 PQC
实现保持独立文件；源码数量不决定算子数量。

可选的 PyTorch / torch_npu 适配层放在顶层 `torch_extension/`，独立构建并
复用原生核心，负责 Tensor、设备上下文、流和框架注册；核心库独立于
PyTorch 构建。仅给实际需要框架调用的算子增加绑定，不强制每个算子
建立适配目录或复制一套工程。

测试分类与统一入口见[测试说明](../tests/README.md)。目录调整后必须核对
测试发现数量和执行结果，设备行为变更应验证真实设备路径。

设备侧可组合 API 与 SDK 发布接口需要单独设计；源码目录统一不代表
已经具备在其他 Kernel 内部复用整个算子的能力。
