# AICE 编译、测试与性能测试

简体中文 | [English](build_en.md)

以下命令在 cann-crypto 仓库根目录执行。Host 构建需要 CMake 3.16+、C99 和 C++17 编译器；
设备构建需要 CANN Toolkit，设备测试和性能测试在对应昇腾设备上执行。

## 编译

加载 CANN 环境并选择目标设备：

```bash
source /usr/local/Ascend/ascend-toolkit/set_env.sh
export ASCEND_COMPUTE_UNIT=ascend910b
# Ascend 950 使用 ascend950
bash build.sh --components=algorithms/cipher/aice --pkg
```

默认使用 Release 构建，产物位于 `src/algorithms/cipher/aice/build/`：

| 产物 | 用途 |
| --- | --- |
| libcann_crypto_aice.so | AICE 共享库 |
| aice_example | 加解密调用示例 |
| bench/aice_bench | 加密性能测试程序 |

两个目标都编译 `device_dav2201.h`，由目标架构选择相应算术实现。

## 调用示例

示例使用 batch=128、dim=1 对 1024 字节数据加密、解密并检查恢复结果。
参数为 ACL 逻辑设备号，默认 0：

```bash
./src/algorithms/cipher/aice/build/aice_example 0
```

成功时输出 `AICE_EXAMPLE=PASS`。源码见 [aice_example.cpp](../examples/aice_example.cpp)。

## 测试

Host 单元测试：

```bash
bash build.sh --components=algorithms/cipher/aice -u --ophost
```

设备测试：

```bash
bash build.sh --components=algorithms/cipher/aice --st
```

Host 构建目录为 `src/algorithms/cipher/aice/build/host`。
设备测试构建目录为 `src/algorithms/cipher/aice/build/device-${ASCEND_COMPUTE_UNIT}`。
`--st` 执行功能自验证、CPU 交叉测试、尾块测试和设备 KAT，测试内容见[测试事项](../tests/README.md)。

完成设备测试构建后，可单独执行一项或运行冒烟测试：

```bash
AICE_TEST_BUILD="src/algorithms/cipher/aice/build/device-${ASCEND_COMPUTE_UNIT}"
ctest --test-dir "$AICE_TEST_BUILD" -R '^aice_cross$' --output-on-failure
ctest --test-dir "$AICE_TEST_BUILD" -R '^aice_device_kat$' --output-on-failure
"$AICE_TEST_BUILD/bin/test_aice_device" --self-test --smoke
"$AICE_TEST_BUILD/bin/test_aice_device" --cross-test --smoke
"$AICE_TEST_BUILD/bin/test_aice_device_kat" --smoke
```

## 性能测试

构建后直接运行默认尺寸矩阵：

```bash
./src/algorithms/cipher/aice/build/bench/aice_bench
```

默认 batch=512、dim=1、warmup=3、iterations=10，依次测试
1、4、16、64、256 KiB，以及 1、4、16、64 MiB。

指定尺寸和运行参数：

```bash
./src/algorithms/cipher/aice/build/bench/aice_bench \
  --size-bytes=1048576 --batch=512 --dim=1 --warmup=3 --iterations=100
```

| 参数 | 含义 | 默认值 |
| --- | --- | --- |
| --size-bytes | 每次加密的字节数；指定后只测该尺寸 | 九档尺寸矩阵 |
| --batch | 每个 block 的 lane 数：128、256、512 | 512 |
| --dim | 启动 block 数 | 1 |
| --warmup | 预热次数，可为 0 | 3 |
| --iterations | 计时区间内的启动次数，至少为 1 | 10 |

每个尺寸先与 CPU 对照校验密文，再预热。
两个 ACL Event 之间连续异步启动 iterations 次 kernel，在结束事件后统一同步。
计时包括 kernel 内的状态初始化、GM/UB 搬运和加密计算；内存分配、
Host/Device 拷贝、CPU 校验与预热位于计时区间外。

输出 `kernel_us` 为事件区间折算的平均每次 kernel 耗时，`Gbps` 为十进制吞吐：

```text
kernel_us = elapsed_ms * 1000 / iterations
Gbps = size_bytes * 8 / kernel_us / 1000
```

## CMake 配置与安装

需要独立构建目录、设备号或安装路径时，使用组件 CMake：

```bash
cmake -S src/algorithms/cipher/aice -B build-aice-device \
  -DCMAKE_BUILD_TYPE=Release -DCANN_BUILD_DEVICE=ON \
  -DCANN_BUILD_TESTS=ON -DCANN_BUILD_BENCHMARKS=ON \
  -DASCEND_COMPUTE_UNIT=ascend910b \
  -DASCEND_CANN_PACKAGE_PATH="$ASCEND_HOME_PATH" -DCANN_DEVICE_ID=0
cmake --build build-aice-device -j
ctest --test-dir build-aice-device --output-on-failure
cmake --install build-aice-device --prefix "$PWD/install-aice"
```

| 配置项 | 含义 | 默认值 |
| --- | --- | --- |
| CANN_BUILD_DEVICE | 构建设备共享库 | OFF |
| CANN_BUILD_TESTS | 构建 Host 测试及设备 ST | ON |
| CANN_BUILD_BENCHMARKS | 设备构建时生成 benchmark | ON |
| ASCEND_COMPUTE_UNIT | ascend910b 或 ascend950 | ascend910b |
| ASCEND_CANN_PACKAGE_PATH | CANN Toolkit 路径 | ASCEND_HOME_PATH |
| CANN_DEVICE_ID | ST 和 benchmark 使用的 ACL 逻辑设备号 | 0 |

Host-only 配置：

```bash
cmake -S src/algorithms/cipher/aice -B build-aice-host \
  -DCMAKE_BUILD_TYPE=Release -DCANN_BUILD_DEVICE=OFF -DCANN_BUILD_TESTS=ON
cmake --build build-aice-host -j
ctest --test-dir build-aice-host --output-on-failure
```

安装生成 `lib/libcann_crypto_aice.so` 和 `include/cann_crypto/aice.h`。
调用方包含该头文件，链接 `-lcann_crypto_aice`，运行时加载配套 CANN runtime。
