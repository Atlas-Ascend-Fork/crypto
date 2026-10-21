# NTT

简体中文 | [English](README_en.md)

NTT 组件使用原生 Ascend C Kernel 直调。Host 负责参数检查、Tiling 和启动，设备侧保留通用 NTT、four-step 和 PQC 路径的分文件实现。目录分层不引入 ACLNN 注册或 PyTorch 依赖。

## 目录

| 目录 | 职责 |
| --- | --- |
| `op_host/` | 参数和缓冲区检查、Tiling、运行时启动、内部 C++ 接口及数据布局工具 |
| `op_kernel/` | Kernel 入口、Cube/Vector 计算、PQC 路径、阶段调度和共享 Tiling 数据 |
| `tests/ut/op_host/` | 不需要 CANN 的 Host、数据生成器和命令行回归 |
| `tests/ut/op_kernel/` | 使用 tikicpulib 执行真实 Kernel 的 CPU 单元测试 |
| `tests/st/` | 原生接口的设备端到端正确性测试入口 |
| `tests/assets/` | 固定的生成器 golden 摘要 |
| `examples/` | 设备内存和 stream 管理、原生调用示例 |
| `scripts/` | 示例和测试共用的数据生成、比较和性能验收脚本 |
| `cmake/` | 设备构建、安装和打包逻辑 |

`op_kernel/ntt.cpp` 是薄入口，具体计算继续按职责拆分：`ntt_device_pqc.hpp` 处理 PQC 路径，`ntt_device_subntt.hpp` 处理子 NTT，`ntt_device_phase2.hpp` 处理 four-step 第二阶段，`ntt_device_merge.hpp` 处理合并。无需把这些实现拼接成单个文件。

## 构建与调用

以下命令均从仓库根目录执行。Host 库和测试仅需要 C++17 编译器、CMake 3.16+ 和 Python 3.9+：

```bash
cmake -S src/math/ntt -B build/ntt-host -DBUILD_TESTING=ON
cmake --build build/ntt-host -j 4
ctest --test-dir build/ntt-host -L host_ut --output-on-failure
```

设备构建需要已安装的 CANN SDK；使用实际芯片型号，例如 A2 的 `Ascend910B3`：

```bash
bash src/math/ntt/run.sh --stage build -r npu -v Ascend910B3 -i /path/to/cann
```

Host 实现编译为 `cann_crypto_ntt`，设备启动包装编译为 `cann_crypto_ntt_device`，Kernel 编译为 `ntt_kernels`。`op_kernel/ntt.cpp` 继续通过 `ascendc_library` 构建，无需为目录整理修改源文件后缀。示例通过 `ACLRT_LAUNCH_KERNEL` 对应的原生包装调用 Kernel。

`op_host/ntt_internal.h` 中的 `NttParams`、`NttBuffers` 和 `LaunchNtt` 仍是内部 C++17 接口，不是稳定的公共 C ABI。调用者持有设备缓冲区和 stream；启动接口不主动同步，缓冲区必须保持有效直到 stream 完成。安装后的内部头文件路径仍为 `include/cann_crypto/ntt/ntt_internal.h`。

完整验证命令和依赖说明见[测试说明](tests/README.md)。性能验收继续使用 `scripts/kem_entry.sh`、`scripts/dsa_entry.sh`、`scripts/rns_entry.sh`；`scripts/run_npu_server.sh` 保留已有调用兼容性。
