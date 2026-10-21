# NTT tests

English | [简体中文](README.md)

UT validates independent units; ST exercises data generation, device launch, and complete output through the native interface. Host/Kernel identifies the layer under test. Kernel UT uses CPU debugging libraries without an NPU and does not replace device ST.

| Location / test name | Coverage | Dependencies and labels |
| --- | --- | --- |
| `ut/op_host/` / `ntt_host` | Parameters, Tiling, workspace, and backend call contracts | C++17; `ut;host_ut` |
| `ut/op_host/` / `ntt_data` | Generator goldens, invalid inputs, and generic/PQC layout agreement with production Host Tiling | Python 3.9+, C++17; `ut;host_ut` |
| `ut/op_host/` / `ntt_runner` | CLI arguments, SDK selection, and compatibility entry point | Python 3.9+; `ut;host_ut` |
| `ut/op_kernel/` / `ntt_kernel_ut` | CPU debugging of the actual mixed Cube/Vector kernel, with 13 golden cases and a Tiling copy case | CANN tikicpulib, ABI=0 GTest; `ut;kernel_ut` |
| `st/run.sh` | ML-KEM, ML-DSA, or generic/RNS NTT through the native device interface, followed by full output comparison | CANN, NPU; standalone script |
| `assets/ntt_data_golden.json` | Fixed-seed generator and matrix digests | Read by `ntt_data` |

Run all commands below from the repository root.

## Host UT

```bash
cmake -S src/math/ntt -B build/ntt-host -DBUILD_TESTING=ON
cmake --build build/ntt-host -j 4
ctest --test-dir build/ntt-host -L host_ut --output-on-failure
```

The default configuration registers these three Host tests without CANN. Test configuration requires Python so a missing interpreter cannot silently omit two regression entry points.

## CPU Kernel UT

When enabled, CMake finds GTest and tikicpulib and builds the real `ntt_kernel_ut` target. Missing dependencies produce an explicit error rather than success without kernel cases. GTest must use the same `_GLIBCXX_USE_CXX11_ABI=0` as the CANN CPU debugging library. You can build GTest in a separate directory:

```bash
cmake -S /path/to/googletest -B build/gtest-abi0 \
  -DCMAKE_CXX_FLAGS=-D_GLIBCXX_USE_CXX11_ABI=0 \
  -DCMAKE_INSTALL_PREFIX="$PWD/build/gtest-abi0/install" -DBUILD_GMOCK=OFF
cmake --build build/gtest-abi0 -j 4
cmake --install build/gtest-abi0
```

Load the environment for the selected CANN SDK first. Set `GTest_DIR` to the actual installed CMake package directory:

```bash
cmake -S src/math/ntt -B build/ntt-kernel-ut \
  -DBUILD_TESTING=ON -DCRYPTO_NTT_DEVICE=OFF \
  -DCANN_BUILD_CPU_DEBUG_TESTS=ON -DCANN_CPU_DEBUG_SOC=Ascend910B1 \
  -DASCEND_CANN_PACKAGE_PATH="$ASCEND_HOME_PATH" \
  -DGTest_DIR="$PWD/build/gtest-abi0/install/lib/cmake/GTest"
cmake --build build/ntt-kernel-ut -j 4
ctest --test-dir build/ntt-kernel-ut -L kernel_ut --output-on-failure
```

The target includes `op_kernel/ntt.cpp` directly, executes it through `ICPU_RUN_KF`, and compares its output against goldens from `scripts/gen_data.py`. The single CTest process has a 1800-second default timeout, configurable with `CANN_KERNEL_UT_TIMEOUT`. A Host build with `CANN_BUILD_CPU_DEBUG_TESTS` disabled does not establish that Kernel UT passed.

## Device ST and performance acceptance

Load the CANN environment first. Select the actual chip model on the machine; the example below uses A2 `Ascend910B3`. Each command builds or reuses the device binary, generates data, executes on the NPU, and compares against golden output:

```bash
bash src/math/ntt/tests/st/run.sh -r npu -v Ascend910B3 --profile ml-kem
bash src/math/ntt/tests/st/run.sh -r npu -v Ascend910B3 --profile ml-dsa
bash src/math/ntt/tests/st/run.sh -r npu -v Ascend910B3 --profile rns
```

The default ML-KEM/ML-DSA batch is 4096 polynomials; RNS defaults to N=65536 and logP=200. Existing `NTT_*` environment variables select boundary cases, such as the generic generator layout regression for issue #18:

```bash
NTT_N=256 NTT_Q=3329 NTT_G=17 NTT_BLOCK_DIM=16 NTT_W=16 \
  NTT_BENCH=1 NTT_RNS_CHANNELS=1 NTT_LOGP=0 \
  bash src/math/ntt/tests/st/run.sh -r npu -v Ascend910B3 --profile rns
NTT_N=256 NTT_Q=8380417 NTT_G=10 NTT_BLOCK_DIM=16 NTT_W=16 \
  NTT_BENCH=1 NTT_RNS_CHANNELS=1 NTT_LOGP=0 \
  bash src/math/ntt/tests/st/run.sh -r npu -v Ascend910B3 --profile rns
```

Use `-i /path/to/cann` to select the SDK explicitly. The example uses ACL logical device 0. Logs and binary data are written under the component's `build/acceptance/` directory by default; use `NTT_RUN_DIR` to select a separate directory. Test failures return a nonzero status. See the section above for the CPU Kernel UT entry and dependencies.

This script forwards to the component's `run.sh` without duplicating algorithms, golden comparisons, or build logic. `-r sim -v Ascend910B1` uses the existing Camodel path and requires a matching SDK and simulator. Report simulator results separately from real NPU results.

The default Host CTest suite does not launch NPU jobs. Performance acceptance scripts remain in `../scripts/`. Report performance thresholds and correctness results separately.
