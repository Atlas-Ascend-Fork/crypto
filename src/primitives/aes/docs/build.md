# 构建与测试

简体中文 | [English](build_en.md)

设备构建使用已安装 CANN 的 `ASC` CMake 工具链。请使用新的构建目录，避免混用缓存。

先从仓库根目录进入组件目录；下文各节命令均在此目录执行：

```bash
cd src/primitives/aes
```

## Host 构建及测试

Host UT 仅需 C++17 编译器与 CMake，无需 CANN：

```bash
cmake -S . -B build/host -DCANN_BUILD_DEVICE=OFF -DCANN_BUILD_TESTS=ON
cmake --build build/host --parallel
ctest --test-dir build/host -L host --output-on-failure
```

## A2 设备构建及测试

需要 Linux、CANN 9.0.0 和对应的驱动环境。设备 ST 另需 PyCryptodome：

```bash
source /usr/local/Ascend/cann-9.0.0/set_env.sh
python3 -m pip install pycryptodome

cmake -S . -B build/device \
  -DCMAKE_BUILD_TYPE=Release \
  -DCANN_BUILD_DEVICE=ON \
  -DCANN_BUILD_TESTS=ON \
  -DASCEND_CANN_PACKAGE_PATH="$ASCEND_HOME_PATH" \
  -DASCEND_COMPUTE_UNIT=ascend910b \
  -DPython3_EXECUTABLE="$(command -v python3)" \
  -DCANN_DEVICE_ID=0 &&
cmake --build build/device --parallel 8 &&
ctest --test-dir build/device -L device --output-on-failure --no-tests=error
```

`CANN_DEVICE_ID` 可选择测试设备。生成的设备库为 `build/device/libcann_crypto_aes.so`。

## 安装

```bash
cmake --install build/device --prefix "$PWD/install/aes"
```

安装结果包含 `lib/libcann_crypto_aes.so` 和 `include/cann_crypto/aes.h`。