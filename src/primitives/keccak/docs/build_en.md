# Build and Test

English | [简体中文](build.md)

Like the repository's AES component, device builds use the installed CANN `ASC` CMake toolchain to compile `.asc` files and launch kernels through `<<<>>>`.
Use a new build directory to avoid reusing caches from the previous ACLNN project.

## Build and Test on A2

Linux, CANN 9.1.0, and the corresponding driver environment are required:

```bash
source /usr/local/Ascend/cann-9.1.0/set_env.sh
cd /workspace/crypto/src/primitives/keccak

cmake -S . -B build/device \
  -DCMAKE_BUILD_TYPE=Release \
  -DCANN_BUILD_DEVICE=ON \
  -DCANN_BUILD_TESTS=ON \
  -DCANN_BUILD_CPU_DEBUG_TESTS=OFF \
  -DASCEND_CANN_PACKAGE_PATH="$ASCEND_HOME_PATH" \
  -DASCEND_COMPUTE_UNIT=ascend910b \
  -DPython3_EXECUTABLE="$(command -v python3)" \
  -DCANN_DEVICE_ID=0 &&
cmake --build build/device --parallel 8 &&
ctest --test-dir build/device --output-on-failure --no-tests=error
```

The device library is `build/device/libcann_crypto_keccak.so`. Device tests include a direct-launch example and a comparison against an independent reference implementation.

## Build and Test on A3

Use the commands above with build directory `build/device-a3` and compilation target `-DASCEND_COMPUTE_UNIT=ascend910_93`.
Both A2 and A3 use the `dav-2201` architecture. The actual number of AIV cores is queried at runtime.

## Install and Link

```bash
cmake --install build/device --prefix "$PWD/build/install"
```

The installation contains `lib/libcann_crypto_keccak.so` and `include/cann_crypto/keccak.h`.
Callers link against `cann_crypto_keccak` and must initialize ACL, select the device, and create a stream before calling the interface.
Add the installation's `lib` directory to `LD_LIBRARY_PATH` at runtime. `ASCEND_CUSTOM_OPP_PATH` does not need to be configured.

## Host Tests

Neither CANN nor GTest needs to be installed:

```bash
cmake -S . -B build/host \
  -DCMAKE_BUILD_TYPE=Release \
  -DCANN_BUILD_DEVICE=OFF \
  -DCANN_BUILD_TESTS=ON \
  -DCANN_BUILD_CPU_DEBUG_TESTS=OFF &&
cmake --build build/host --parallel 8 &&
ctest --test-dir build/host --output-on-failure --no-tests=error
```

## CPU Simulation of Kernel Unit Tests

This requires CANN tikicpulib and GTest built with ABI=0; neither is required for Host tests alone.
Build GTest 1.11.0 with the same compiler, then pass its CMake package directory through `GTest_DIR`:

```bash
cmake -S . -B build/kernel-ut \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCANN_BUILD_DEVICE=OFF \
  -DCANN_BUILD_TESTS=ON \
  -DCANN_BUILD_CPU_DEBUG_TESTS=ON \
  -DASCEND_CANN_PACKAGE_PATH="$ASCEND_HOME_PATH" \
  -DGTest_DIR="/path/to/gtest-abi0/lib/cmake/GTest" \
  -DCANN_CPU_DEBUG_SOC=Ascend910B1 \
  -DCANN_KERNEL_UT_TIMEOUT=1800 &&
cmake --build build/kernel-ut --parallel 8 &&
ctest --test-dir build/kernel-ut -L kernel_ut --output-on-failure --no-tests=error
```

CPU simulation uses the selected SoC model, independently of the NPU model installed in the server.
