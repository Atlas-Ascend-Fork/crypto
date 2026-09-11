# 编译与运行

## 环境要求

- Linux，C++17/C11 编译器，CMake 3.16+，Python 3.8+。
- CANN Toolkit 及其配套的驱动、固件和算子包。Python 版本与第三方依赖应满足所用 CANN 版本的要求。
- 设备差分测试依赖 OpenSSL 1.1.1+ 开发文件。OpenSSL 不参与生产算子编译。

先加载所用 CANN 版本的环境：

```bash
source /usr/local/Ascend/cann-9.1.0/set_env.sh
python3 -c "import sys, numpy, tbe; print(sys.executable); print(numpy.__version__)"
```

`ASCEND_CANN_PACKAGE_PATH` 应指向包含 `include/acl/acl.h` 和 `lib64` 的安装目录。
使用非默认 Python 环境时，将该解释器传给 `Python3_EXECUTABLE`，并在同一环境中安装依赖。

## Host 测试

在算子目录执行，无需 CANN 或 NPU：

```bash
cmake -S . -B build/host -DCMAKE_BUILD_TYPE=Release -DCANN_BUILD_TESTS=ON &&
cmake --build build/host --parallel 8 &&
(cd build/host && ctest --output-on-failure)
```

## 设备构建

| 产品 | ASCEND_COMPUTE_UNIT |
| --- | --- |
| Atlas A2 | ascend910b |
| Atlas A3 | ascend910_93 |

以下为 A3、CANN 9.1.0 的构建命令，产品验证状态见 [README](../README.md)：

```bash
cmake -S . -B build/cann-9.1.0-a3 \
    -DCMAKE_BUILD_TYPE=Release \
    -DCANN_BUILD_DEVICE=ON \
    -DCANN_BUILD_TESTS=ON \
    -DASCEND_COMPUTE_UNIT=ascend910_93 \
    -DASCEND_CANN_PACKAGE_PATH="$ASCEND_HOME_PATH" \
    -DPython3_EXECUTABLE="$(command -v python3)" \
    -DCANN_BUILD_JOBS=8 &&
cmake --build build/cann-9.1.0-a3 --parallel 8 &&
(cd build/cann-9.1.0-a3 && ctest --output-on-failure)
```

A2 使用 `-DASCEND_COMPUTE_UNIT=ascend910b`。切换 CANN 版本或产品型号时，
重新加载对应的环境并使用独立构建目录，例如 `build/cann-8.5.2-a2`，不要复用其他目标的包。

## 构建产物与调用

以 `build/cann-9.1.0-a3` 为例：

- 算子包：`opp/packages/vendors/shake/`。
- ACLNN 调用示例：`examples/example_shake_tensor`。
- 设备正确性测试：`tests/shake_device_test`。

SHAKE 构建复用 Keccak 的构建支持，算子包包含 KeccakF1600Tensor、Shake128Tensor 和 Shake256Tensor。

构建时生成 ACLNN 头文件和接口库，`PACKAGE_SYMBOLS=PASS` 表示头文件和导出符号检查通过。
安装目标写入构建目录，不修改系统 CANN。部署时需保留完整 vendor 包。

```bash
export ASCEND_CUSTOM_OPP_PATH="$PWD/build/cann-9.1.0-a3/opp/packages/vendors/shake"
export LD_LIBRARY_PATH="$ASCEND_CUSTOM_OPP_PATH/op_api/lib:$ASCEND_HOME_PATH/lib64:${LD_LIBRARY_PATH:-}"
export CANN_CRYPTO_SOC_TARGET=ascend910_93
./build/cann-9.1.0-a3/examples/example_shake_tensor 0 128
./build/cann-9.1.0-a3/examples/example_shake_tensor 0 256
```

CTest 自动设置包路径和编译目标，并在设备调用前检查实际 SoC 是否匹配。
手动运行时，`CANN_CRYPTO_SOC_TARGET` 应与包的编译目标一致。设备编号默认为 0，可通过
`-DCANN_DEVICE_ID=1` 等配置指定其他 ACL 逻辑设备。

## 构建选项

| 选项 | 默认值 | 说明 |
| --- | --- | --- |
| CANN_BUILD_DEVICE | OFF | 构建设备包及调用示例 |
| CANN_BUILD_TESTS | ON | 构建并注册测试 |
| CANN_BUILD_CPU_DEBUG_TESTS | OFF | 使用 GTest、tikicpulib 执行实际 Kernel UT |
| CANN_CPU_DEBUG_SOC | Ascend910B1 | CPU 调试库的完整 SoC 名称 |
| CANN_BUILD_JOBS | 8 | Kernel 编译并行数 |
| CANN_DEVICE_ID | 0 | 设备测试编号 |
| ASCEND_COMPUTE_UNIT | ascend910b | 目标产品 |
| Python3_EXECUTABLE | 自动查找 | 构建及测试共用的 Python 解释器 |

## 故障定位

配置未完成时，不执行编译和测试。先解决配置阶段报告的缺失依赖。
编译失败的临时目录会打印在日志中，重新构建使用新的临时目录，不会覆盖失败现场。

设备调用失败时，程序输出返回码及 `aclGetRecentErrMsg` 的错误详情。进一步诊断可运行：

```bash
ASCEND_GLOBAL_LOG_LEVEL=0 ASCEND_SLOG_PRINT_TO_STDOUT=1 \
ctest --test-dir build/cann-9.1.0-a3 -L device -V
```

测试范围见 [测试说明](../tests/README.md)。
