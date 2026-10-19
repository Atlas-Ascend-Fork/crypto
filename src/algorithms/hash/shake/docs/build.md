# 构建与测试

简体中文 | [English](build_en.md)

使用 CANN 的 `ASC` CMake 工具链编译直调设备库，不生成或安装 OPP vendor 包。
请使用新的构建目录。构建示例不需要 GTest；设备正确性测试另需 OpenSSL 开发包。

## A2 设备构建及测试

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

构建生成 `build/device/libcann_crypto_shake.so` 和 `build/device/keccak/libcann_crypto_keccak.so`。
SHAKE 库包含 SHAKE128、SHAKE256 两个直调接口，Keccak 库供设备回归测试调用。
若只需要设备库及示例，可设置 `-DCANN_BUILD_TESTS=OFF`。

## A3 设备构建及测试

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

A2、A3 均使用 `dav-2201` 架构，运行时查询当前设备的 AIV 核数。

## 压力测试

在相应构建目录完成设备测试后执行，无需设置自定义 OPP 路径：

```bash
python3 tests/stress.py --build-dir build/device --device 0 --runs 100
```

A3 将目录替换为 `build/device-a3`。每轮执行 SHAKE128、SHAKE256 各两个代表参数用例；全部成功时输出 `STRESS_RESULT=100/100`。
日志保存在 `<build-dir>/results/stress/`。

## 安装及链接

```bash
cmake --install build/device --prefix "$PWD/build/install"
```

安装 `lib/libcann_crypto_shake.so`、`lib/libcann_crypto_keccak.so` 及相应公共 C 头文件。
调用方链接 `cann_crypto_shake`，初始化 ACL 并传入当前设备上的 stream。
运行时将安装目录的 `lib` 加入 `LD_LIBRARY_PATH`。

## Host 测试

无需安装 CANN 或 GTest：

```bash
cmake -S . -B build/host \
  -DCMAKE_BUILD_TYPE=Release \
  -DCANN_BUILD_DEVICE=OFF \
  -DCANN_BUILD_TESTS=ON \
  -DCANN_BUILD_CPU_DEBUG_TESTS=OFF &&
cmake --build build/host --parallel 8 &&
ctest --test-dir build/host --output-on-failure --no-tests=error
```

## CPU 模拟 Kernel UT

此项需要 CANN tikicpulib 和 ABI=0 的 GTest；仅运行 Host 测试时不需要它们。
使用同一编译器构建 GTest 1.11.0 后，将其 CMake 包路径传给 `GTest_DIR`：

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

CPU 模拟使用所选 SoC 模型，不依赖服务器实际安装的 NPU 型号。
