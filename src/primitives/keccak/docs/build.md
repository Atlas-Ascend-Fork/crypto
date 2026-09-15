# 编译与运行

## 环境要求

- Linux，C++17/C11 编译器，CMake 3.16+，Python 3.8+。
- CANN Toolkit 及其配套的驱动、固件和算子包。Python 版本与第三方依赖应满足所用 CANN 版本的要求。
- 纯 C 参考测试不依赖 CANN 或 OpenSSL。

先加载所用 CANN 版本的环境：

```bash
source /usr/local/Ascend/cann-9.1.0/set_env.sh
python3 -c "import sys, numpy, tbe; print(sys.executable); print(numpy.__version__)"
```

`ASCEND_CANN_PACKAGE_PATH` 应指向包含 `include/acl/acl.h` 和 `lib64` 的安装目录。
使用非默认 Python 环境时，将该解释器传给 `Python3_EXECUTABLE`，并在同一环境中安装依赖。

设备构建使用 CANN 安装目录中的 `ascendc_kernel_cmake/fwk_modules`，不复制 Toolkit 的编译脚本。
配置时自动查找 `compiler/tikcpp` 和 `tools/tikcpp` 下的框架，兼容安装目录中的
`toolkit` 和架构子目录。非默认安装布局可通过 `-DCANN_CMAKE_DIR=/path/to/fwk_modules` 指定。
所选框架须提供 `npu_op_package`、`npu_op_code_gen` 和 `npu_op_kernel_library` 等算子工程接口。

## Host 测试

在算子目录执行，无需 CANN 或 NPU：

```bash
cmake -S . -B build/host \
    -DCMAKE_BUILD_TYPE=Release \
    -DCANN_BUILD_DEVICE=OFF \
    -DCANN_BUILD_TESTS=ON \
    -DCANN_BUILD_CPU_DEBUG_TESTS=OFF &&
cmake --build build/host --parallel 8 &&
ctest --test-dir build/host --output-on-failure --no-tests=error
```

## Kernel UT

Kernel UT 使用 GTest 和 CANN 的 `tikicpulib` 在 CPU 上运行实际 Ascend C Kernel，无需 NPU。
当前测试使用 `_GLIBCXX_USE_CXX11_ABI=0`，GTest 必须使用同一编译器和 ABI 构建。
配置阶段会检查 GTest 是否能够以 ABI=0 编译并链接。

### 准备 GTest

已安装 ABI=0 的 GTest 时，可直接在下一步指定其 `GTest_DIR`。
否则，以下以官方 GoogleTest 1.11.0 源码为例，假定已解压到
`build/googletest-release-1.11.0`。网络受限时，可在其他机器下载源码后上传。

在当前算子目录执行，安装目录仅用于本工程测试：

```bash
cmake -S build/googletest-release-1.11.0 \
    -B build/gtest-1.11.0-abi0-build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=/usr/bin/c++ \
    -DCMAKE_CXX_FLAGS="-D_GLIBCXX_USE_CXX11_ABI=0" \
    -DBUILD_GMOCK=OFF \
    -DBUILD_SHARED_LIBS=OFF \
    -DINSTALL_GTEST=ON \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_INSTALL_PREFIX="$PWD/build/gtest-1.11.0-abi0-prefix" &&
cmake --build build/gtest-1.11.0-abi0-build --parallel 8 &&
cmake --install build/gtest-1.11.0-abi0-build
```

使用 CMake 4.x 构建 GoogleTest 1.11.0 时，在上述配置命令中另加
`-DCMAKE_POLICY_VERSION_MINIMUM=3.5`，以兼容该版本源码的旧 CMake 策略。

### 编译与执行

先加载所用 CANN 的 `set_env.sh`。以下命令统一使用 `build/cpu-debug`，
不构建设备包。若已有其他名称的构建目录，配置、编译和执行都应使用同一路径。

```bash
set -o pipefail
ut_build="$PWD/build/cpu-debug"

cmake -S . -B "$ut_build" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_COMPILER=/usr/bin/c++ \
    -DCANN_BUILD_DEVICE=OFF \
    -DCANN_BUILD_TESTS=ON \
    -DCANN_BUILD_CPU_DEBUG_TESTS=ON \
    -DASCEND_CANN_PACKAGE_PATH="$ASCEND_HOME_PATH" \
    -DPython3_EXECUTABLE="$(command -v python3)" \
    -DGTest_DIR="$PWD/build/gtest-1.11.0-abi0-prefix/lib/cmake/GTest" \
    -DCANN_CPU_DEBUG_SOC=Ascend910B1 \
    -DCANN_KERNEL_UT_TIMEOUT=1800 &&
cmake --build "$ut_build" --parallel 8 &&
mkdir -p "$ut_build/results" &&
ctest --test-dir "$ut_build" -L kernel_ut \
    --output-on-failure --no-tests=error \
    2>&1 | tee "$ut_build/results/kernel-ut.log"
```

`GTest_DIR` 应指向所选安装目录中的 `GTestConfig.cmake` 所在文件夹。
`CANN_CPU_DEBUG_SOC` 是 `tikicpulib` 提供的完整目标名称，不是设备编译目标缩写。
上述 `Ascend910B1` 配置在 A2 或 A3 服务器上运行时，均使用该 CPU 模拟目标；
A2、A3 的实际设备验证按设备构建章节执行。

超时按每个测试程序计算，默认 1800 秒；需要更长时间时重新配置
`-DCANN_KERNEL_UT_TIMEOUT=3600` 等正整数。

## 设备构建

| 产品 | ASCEND_COMPUTE_UNIT |
| --- | --- |
| Atlas A2 | ascend910b |
| Atlas A3 | ascend910_93 |

以下为 A3、CANN 9.1.0 的构建命令，产品验证状态见 [README](../README.md)：

```bash
cmake -S . -B build/cann-9.1.0-a3-sdk \
    -DCMAKE_BUILD_TYPE=Release \
    -DCANN_BUILD_DEVICE=ON \
    -DCANN_BUILD_TESTS=ON \
    -DCANN_BUILD_CPU_DEBUG_TESTS=OFF \
    -DASCEND_COMPUTE_UNIT=ascend910_93 \
    -DASCEND_CANN_PACKAGE_PATH="$ASCEND_HOME_PATH" \
    -DPython3_EXECUTABLE="$(command -v python3)" \
    -DCANN_BUILD_JOBS=8 &&
cmake --build build/cann-9.1.0-a3-sdk --parallel 8 &&
ctest --test-dir build/cann-9.1.0-a3-sdk --output-on-failure --no-tests=error
```

A2 使用 `-DASCEND_COMPUTE_UNIT=ascend910b`，并将上述构建目录替换为 `build/cann-9.1.0-a2-sdk`。
切换 CANN 版本或产品型号时，
重新加载对应的环境并使用独立构建目录，例如 `build/cann-8.5.2-a2`，不要复用其他目标的包。
从本地模板构建切换到 CANN 框架构建时，也须使用新的构建目录。

## 构建产物与调用

以 `build/cann-9.1.0-a3-sdk` 为例：

- 算子包：`opp/packages/vendors/keccak/`。
- ACLNN 调用示例：`examples/example_keccak_f1600_tensor`。
- 设备正确性测试：`tests/st/keccak_device_test`。

构建时生成 ACLNN 头文件和接口库，`PACKAGE_SYMBOLS=PASS` 表示头文件和导出符号检查通过。
安装目标写入构建目录，不修改系统 CANN。部署时需保留完整 vendor 包。

```bash
export ASCEND_CUSTOM_OPP_PATH="$PWD/build/cann-9.1.0-a3-sdk/opp/packages/vendors/keccak"
export LD_LIBRARY_PATH="$ASCEND_CUSTOM_OPP_PATH/op_api/lib:$ASCEND_HOME_PATH/lib64:${LD_LIBRARY_PATH:-}"
export CANN_CRYPTO_SOC_TARGET=ascend910_93
./build/cann-9.1.0-a3-sdk/examples/example_keccak_f1600_tensor 0
```

CTest 自动设置包路径和编译目标，并在设备调用前检查实际 SoC 是否匹配。
手动调用 A2 包时，同时使用 `build/cann-9.1.0-a2-sdk` 路径并设置
`CANN_CRYPTO_SOC_TARGET=ascend910b`。
手动运行时，`CANN_CRYPTO_SOC_TARGET` 应与包的编译目标一致。设备编号默认为 0，可通过
`-DCANN_DEVICE_ID=1` 等配置指定其他 ACL 逻辑设备。

## 构建选项

| 选项 | 默认值 | 说明 |
| --- | --- | --- |
| CANN_BUILD_DEVICE | OFF | 构建设备包及调用示例 |
| CANN_BUILD_TESTS | ON | 构建并注册测试 |
| CANN_BUILD_CPU_DEBUG_TESTS | OFF | 使用 GTest、tikicpulib 执行实际 Kernel UT |
| CANN_CPU_DEBUG_SOC | Ascend910B1 | CPU 调试库的完整 SoC 名称 |
| CANN_KERNEL_UT_TIMEOUT | 1800 | 每个 Kernel UT 程序的超时秒数，须为正整数 |
| CANN_BUILD_JOBS | 8 | Kernel 编译并行数 |
| CANN_DEVICE_ID | 0 | 设备测试编号 |
| ASCEND_COMPUTE_UNIT | ascend910b | 目标产品 |
| CANN_CMAKE_DIR | 自动查找 | CANN 安装目录中的算子 CMake 框架 |
| Python3_EXECUTABLE | 自动查找 | 构建及测试共用的 Python 解释器 |

## 故障定位

配置未完成时，不执行编译和测试。先解决配置阶段报告的缺失依赖。
配置日志会输出 `CANN operator framework` 路径。框架模块或接口缺失时，检查所选 Toolkit
及 `CANN_CMAKE_DIR`，不要混用其他 CANN 版本的编译脚本。

设备调用失败时，程序输出返回码及 `aclGetRecentErrMsg` 的错误详情。进一步诊断可运行：

```bash
ASCEND_GLOBAL_LOG_LEVEL=0 ASCEND_SLOG_PRINT_TO_STDOUT=1 \
ctest --test-dir build/cann-9.1.0-a3-sdk -L device -V
```

测试范围见 [测试说明](../tests/README.md)。
