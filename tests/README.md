# 测试

简体中文 | [English](README_en.md)

UT / ST 表示测试范围，Host / Kernel 表示被测代码层，两者分别组织。

| 位置 | 内容 | 环境 |
| --- | --- | --- |
| [`tests/ut/common/`](ut/common/README.md) | 仓库级公共回归 | 按各测试的依赖要求 |
| 组件 [`tests/ut/op_host/`](ut/op_host/README.md) | 参数校验、Tiling、调度、运行时 mock | C/C++、Python；无需设备 |
| 组件 [`tests/ut/op_kernel/`](ut/op_kernel/README.md) | Kernel 计算逻辑的 CPU 调测 | CANN tikicpulib、ABI 匹配的 GTest；无需设备 |
| 组件 `tests/st/` | 通过公开接口执行真实 Kernel 并比较 golden | CANN 与昇腾设备 |
| 组件 `tests/assets/` | 独立参考实现、固定向量和数据 | 由测试使用 |

```bash
bash build.sh -u --ophost       # Host UT
bash build.sh -u --opkernel     # CPU Kernel UT
bash build.sh --st             # 设备 ST
bash build.sh -u               # 全部单元测试层次，不含 ST
```

CTest 组件的 Host 路径排除 `device`、`kernel_ut` 标签；CPU Kernel UT 启用
`CANN_BUILD_CPU_DEBUG_TESTS=ON` 并只选择 `kernel_ut`；设备 ST 选择 `device`。
NTT 的设备验证保留组件脚本入口。缺少明确请求的 CANN 依赖时返回失败。
单用例默认超时为 1800 秒，包含较慢的 CPU Kernel 仿真；可通过 `--ut_timeout=<秒数>` 覆盖。

`--allow_empty` 仅允许整个请求未发现可执行测试入口的情况；CTest 筛选后零用例、构建失败或测试失败仍返回失败。

PR Host 门禁、CPU Kernel UT、设备 ST 分开运行。Host mock 和 CPU 模型不能代替
设备的 DMA、同步和数值正确性验证。设备冒烟入口为 `scripts/ci/smoke.sh`。
框架适配用例随适配层放置，按其说明独立运行。

NIST ACVP、Wycheproof 与 GM/T 向量获取脚本仍在规划中，见[辅助脚本说明](../scripts/README.md)。
当前随源码提供的参考实现与已知答案见 [Keccak](../src/primitives/keccak/tests/README.md)
和 [SHAKE](../src/algorithms/hash/shake/tests/README.md) 测试说明。

其他公开接口层用例见 [op_api](ut/op_api/README.md)。
