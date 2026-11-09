# Build and Test

English | [简体中文](build.md)

Device builds use the `ASC` CMake toolchain from the installed CANN package. Use a fresh build directory.

From the repository root, enter the component directory first. Run all commands below from this directory:

```bash
cd src/primitives/aes
```

## Host Build and Tests

Host UT requires only a C++17 compiler and CMake; CANN is not needed:

```bash
cmake -S . -B build/host -DCANN_BUILD_DEVICE=OFF -DCANN_BUILD_TESTS=ON
cmake --build build/host --parallel
ctest --test-dir build/host -L host --output-on-failure
```

## A2 Device Build and Tests

Requires Linux, CANN 9.0.0, and the matching driver. Device ST also requires PyCryptodome:

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

`CANN_DEVICE_ID` selects the test device. The build produces `build/device/libcann_crypto_aes.so`.

## Installation

```bash
cmake --install build/device --prefix "$PWD/install/aes"
```

The install produces `lib/libcann_crypto_aes.so` and `include/cann_crypto/aes.h`.