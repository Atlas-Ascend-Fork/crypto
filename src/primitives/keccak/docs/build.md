# 构建与测试

简体中文 | [English](build_en.md)

设备构建与仓库 AES 一样使用已安装 CANN 的 `ASC` CMake 工具链，编译 `.asc` 并通过 `<<<>>>` 启动 kernel。
请使用新的构建目录，避免混用迁移前的 ACLNN 工程缓存。

## A2 设备构建及测试

需要 Linux、CANN 9.1.0 和对应的驱动环境：

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

生成的设备库为 `build/device/libcann_crypto_keccak.so`；设备用例包括直调示例和独立参考比较程序。

## A3 设备构建及测试

沿用上述命令，将构建目录改为 `build/device-a3`，编译目标改为 `-DASCEND_COMPUTE_UNIT=ascend910_93`。
A2、A3 的底层编译架构均为 `dav-2201`，运行时查询实际设备的 AIV 核数。

## 安装及链接

```bash
cmake --install build/device --prefix "$PWD/build/install"
```

安装内容为 `lib/libcann_crypto_keccak.so` 和 `include/cann_crypto/keccak.h`。
调用方链接 `cann_crypto_keccak` 并先初始化 ACL、设置设备、创建 stream。
运行时将安装目录的 `lib` 加入 `LD_LIBRARY_PATH`。无需配置 `ASCEND_CUSTOM_OPP_PATH`。

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
