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
cmake -S src/math/ntt -B build/ntt-host -DCANN_BUILD_TESTS=ON
cmake --build build/ntt-host -j 4
ctest --test-dir build/ntt-host -L host_ut --output-on-failure
```

设备构建使用 CANN 9.0.0 的原生 ASC CMake 接口，与 AES、Keccak、SHAKE 一致。`ASCEND_COMPUTE_UNIT` 默认为 `ascend910b`（A2），A3 使用 `ascend910_93`；两者均编译为 `dav-2201`，无需单独设置 NTT 的 `SOC_VERSION`。

```bash
source /path/to/cann-9.0.0/set_env.sh
ASCEND_COMPUTE_UNIT=ascend910b bash build.sh --pkg --components=math/ntt
ASCEND_COMPUTE_UNIT=ascend910b bash build.sh --st --components=math/ntt
# A3：将 ascend910b 改为 ascend910_93；可同时选择 AES、Keccak、SHAKE。
```

独立脚本使用相同的目标参数，`-v` 优先于环境变量：

```bash
bash src/math/ntt/run.sh --stage build -r npu -v ascend910b -i /path/to/cann-9.0.0
bash src/math/ntt/run.sh --stage env -v ascend910_93 -i /path/to/cann-9.0.0
```

`--stage env` 显示 SDK、架构族和编译架构，不构建、不查询设备。旧调用中的 `Ascend910B3` 等完整型号仍作为对应架构族的别名接受。真机测试会校验设备与目标架构族是否匹配。`sim` 使用 Ascend910B1 模型，运行 `-r sim -v ascend910b`；仿真型号与设备编译目标分别处理。

Host 实现编译为 `cann_crypto_ntt`；设备库 `cann_crypto_ntt_device` 通过 `.asc` 包装以 `<<<...>>>` 调用 Kernel。`op_kernel/ntt.cpp` 与 CPU Kernel UT 共用，不再使用 `ascendc_library` 或生成的 `aclrtlaunch_ntt.h`。根目录设备测试通过 CTest 运行已构建的示例，覆盖 ML-KEM、ML-DSA、RNS 及启动错误返回。

源码头文件 [op_host/ntt_internal.h](op_host/ntt_internal.h) 中的 `NttParams`、`NttBuffers` 和 `LaunchNtt` 仍是内部 C++17 接口，不是稳定的公共 C ABI。调用者持有设备缓冲区和 stream；启动接口不主动同步，缓冲区必须保持有效直到 stream 完成。

设备构建的[安装规则](cmake/npu_lib.cmake)将该文件安装到 `<安装前缀>/include/cann_crypto/ntt/ntt_internal.h`。这是安装目录中的路径，仓库源码根目录下的 `include/cann_crypto/` 不包含该内部头文件；仅构建 Host 测试不会安装它。

上述 `run.sh` 会将设备库和头文件安装到其输出的 `[BUILD] directory` 目录下的 `install/`。如需指定安装位置，将下列 `/path/to/ntt-device-build` 替换为该设备构建目录，再从仓库根目录执行：

```bash
cmake --install /path/to/ntt-device-build --prefix "$PWD/build/ntt-install"
ls build/ntt-install/include/cann_crypto/ntt/ntt_internal.h
```

完整验证命令和依赖说明见[测试说明](tests/README.md)。性能验收继续使用 `scripts/kem_entry.sh`、`scripts/dsa_entry.sh`、`scripts/rns_entry.sh`；`scripts/run_npu_server.sh` 保留已有调用兼容性。
