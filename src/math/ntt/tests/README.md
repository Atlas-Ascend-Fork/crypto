# NTT 测试

简体中文 | [English](README_en.md)

`UT` 按独立单元验证行为，`ST` 通过原生接口验证数据生成、设备启动和完整输出。Host/Kernel 表示被测层次；Kernel UT 使用 CPU 调测库，不占用 NPU，也不能替代设备 ST。

| 位置 / 测试名 | 验证范围 | 依赖与标签 |
| --- | --- | --- |
| `ut/op_host/` / `ntt_host` | 参数、Tiling、workspace 和后端调用契约 | C++17；`ut;host_ut` |
| `ut/op_host/` / `ntt_data` | 生成器 golden、非法输入、通用/PQC 布局与实际 Host Tiling 一致性 | Python 3.9+、C++17；`ut;host_ut` |
| `ut/op_host/` / `ntt_runner` | CLI 参数、SDK 选择和兼容入口 | Python 3.9+；`ut;host_ut` |
| `ut/op_kernel/` / `ntt_kernel_ut` | 实际混合 Cube/Vector Kernel 的 CPU 调测，13 组 golden 用例及 Tiling 拷贝用例 | CANN tikicpulib、ABI=0 GTest；`ut;kernel_ut` |
| `st/` / `ntt_device_*` | ML-KEM、ML-DSA、RNS 完整输出与启动错误返回；`run.sh` 保留独立入口 | CANN、NPU；`st;device` |
| `assets/ntt_data_golden.json` | 固定种子的生成器摘要和矩阵摘要 | 由 `ntt_data` 读取 |

以下命令均从仓库根目录执行。

## Host UT

```bash
cmake -S src/math/ntt -B build/ntt-host -DCANN_BUILD_TESTS=ON
cmake --build build/ntt-host -j 4
ctest --test-dir build/ntt-host -L host_ut --output-on-failure
```

默认注册以上三个 Host 测试，不需要 CANN。测试配置要求 Python，避免缺失解释器时静默漏掉两个回归入口。

## CPU Kernel UT

启用开关后，CMake 查找 GTest 和 tikicpulib 并构建真实的 `ntt_kernel_ut`。缺失依赖将明确报错，不会返回一个没有 Kernel 用例的成功结果。GTest 必须与 CANN CPU 调测库使用相同的 `_GLIBCXX_USE_CXX11_ABI=0`；可在独立目录构建 GTest：

```bash
cmake -S /path/to/googletest -B build/gtest-abi0 \
  -DCMAKE_CXX_FLAGS=-D_GLIBCXX_USE_CXX11_ABI=0 \
  -DCMAKE_INSTALL_PREFIX="$PWD/build/gtest-abi0/install" -DBUILD_GMOCK=OFF
cmake --build build/gtest-abi0 -j 4
cmake --install build/gtest-abi0
```

加载所用 CANN SDK 的环境后执行；`GTest_DIR` 指向实际安装的 CMake 包目录：

```bash
cmake -S src/math/ntt -B build/ntt-kernel-ut \
  -DCANN_BUILD_TESTS=ON -DCANN_BUILD_DEVICE=OFF \
  -DCANN_BUILD_CPU_DEBUG_TESTS=ON -DCANN_CPU_DEBUG_SOC=Ascend910B1 \
  -DASCEND_CANN_PACKAGE_PATH="$ASCEND_HOME_PATH" \
  -DGTest_DIR="$PWD/build/gtest-abi0/install/lib/cmake/GTest"
cmake --build build/ntt-kernel-ut -j 4
ctest --test-dir build/ntt-kernel-ut -L kernel_ut --output-on-failure
```

该目标直接包含 `op_kernel/ntt.cpp`，通过 `ICPU_RUN_KF` 执行并与 `scripts/gen_data.py` 生成的 golden 比较。默认单个 CTest 进程超时为 1800 秒，可通过 `CANN_KERNEL_UT_TIMEOUT` 调整。没有启用 `CANN_BUILD_CPU_DEBUG_TESTS` 的 Host 构建不代表 Kernel UT 已通过。

## 设备 ST 与性能验收

先加载 CANN 9.0.0 环境。设备编译目标与仓库统一：A2 使用 `ascend910b`，A3 使用 `ascend910_93`；每条命令依次完成构建/复用、数据生成、NPU 运行和 golden 比较：

```bash
bash src/math/ntt/tests/st/run.sh -r npu -v ascend910b --profile ml-kem
bash src/math/ntt/tests/st/run.sh -r npu -v ascend910b --profile ml-dsa
bash src/math/ntt/tests/st/run.sh -r npu -v ascend910b --profile rns
```

默认 ML-KEM/ML-DSA 为 4096 个多项式；RNS 为 N=65536、logP=200。可以用现有 `NTT_*` 环境变量选择边界用例，例如 issue #18 的通用生成器布局回归：

```bash
NTT_N=256 NTT_Q=3329 NTT_G=17 NTT_BLOCK_DIM=16 NTT_W=16 \
  NTT_BENCH=1 NTT_RNS_CHANNELS=1 NTT_LOGP=0 \
  bash src/math/ntt/tests/st/run.sh -r npu -v ascend910b --profile rns
NTT_N=256 NTT_Q=8380417 NTT_G=10 NTT_BLOCK_DIM=16 NTT_W=16 \
  NTT_BENCH=1 NTT_RNS_CHANNELS=1 NTT_LOGP=0 \
  bash src/math/ntt/tests/st/run.sh -r npu -v ascend910b --profile rns
```

`-i /path/to/cann` 可以显式选择 SDK。示例使用 ACL 逻辑设备 0，运行日志和二进制数据默认保存在组件 `build/acceptance/` 下，也可通过 `NTT_RUN_DIR` 指定独立目录。测试失败返回非零状态。纯 CPU Kernel UT 的入口和依赖见上文。

该脚本只转发到组件 `run.sh`，不复制算法、golden 比较或构建逻辑。使用 `-r sim -v ascend910b` 时运行 Ascend910B1 Camodel，需要匹配的 SDK 和模拟器；模拟结果应与真实 NPU 结果分开报告。根入口 `bash build.sh --st --components=math/ntt` 通过 CTest 覆盖三种 profile 和启动错误返回，使用相同的 `ASCEND_COMPUTE_UNIT`，无需单独指定 NTT 型号。

默认 Host CTest 不会自动运行 NPU 任务。性能验收脚本仍在 `../scripts/`，性能阈值和正确性结果应分别报告。
