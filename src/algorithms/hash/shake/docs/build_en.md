# Build and Test

English | [简体中文](build.md)

Use CANN's `ASC` CMake toolchain to compile the direct-launch device libraries. No OPP vendor package is generated or installed.
Use a new build directory. Building the examples does not require GTest; device correctness tests additionally require the OpenSSL development package.

## Build and Test on A2

```bash
source /usr/local/Ascend/cann-9.1.0/set_env.sh
cd /workspace/crypto/src/algorithms/hash/shake

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

The build produces `build/device/libcann_crypto_shake.so` and `build/device/keccak/libcann_crypto_keccak.so`.
The SHAKE library contains both SHAKE128 and SHAKE256 direct-launch interfaces. The Keccak library is used by the device regression tests.
Set `-DCANN_BUILD_TESTS=OFF` if only device libraries and examples are needed.

## Build and Test on A3

```bash
source /usr/local/Ascend/cann-9.1.0/set_env.sh
cd /workspace/crypto/src/algorithms/hash/shake

cmake -S . -B build/device-a3 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCANN_BUILD_DEVICE=ON \
  -DCANN_BUILD_TESTS=ON \
  -DCANN_BUILD_CPU_DEBUG_TESTS=OFF \
  -DASCEND_CANN_PACKAGE_PATH="$ASCEND_HOME_PATH" \
  -DASCEND_COMPUTE_UNIT=ascend910_93 \
  -DPython3_EXECUTABLE="$(command -v python3)" \
  -DCANN_DEVICE_ID=0 &&
cmake --build build/device-a3 --parallel 8 &&
ctest --test-dir build/device-a3 --output-on-failure --no-tests=error
```

Both A2 and A3 use the `dav-2201` architecture. The current device's AIV core count is queried at runtime.

## Stress Tests

Run after completing device tests in the corresponding build directory. No custom OPP path is needed:

```bash
python3 tests/stress.py --build-dir build/device --device 0 --runs 100
```

For A3, use directory `build/device-a3`. Each iteration executes two representative cases for each of SHAKE128 and SHAKE256; `STRESS_RESULT=100/100` is printed when all iterations pass.
Logs are saved under `<build-dir>/results/stress/`.

## Install and Link

```bash
cmake --install build/device --prefix "$PWD/build/install"
```

Installs `lib/libcann_crypto_shake.so`, `lib/libcann_crypto_keccak.so`, and the corresponding public C headers.
Callers link against `cann_crypto_shake`, initialize ACL, and pass a stream on the current device.
Add the installation's `lib` directory to `LD_LIBRARY_PATH` at runtime.

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
