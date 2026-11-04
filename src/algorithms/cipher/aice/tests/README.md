# AICE 测试

简体中文 | [English](README_en.md)

执行命令见[编译、测试与性能测试](../docs/build.md)。

## Host 测试

| 测试项 | 测试内容 |
| --- | --- |
| aice_host | C ABI 参数检查、加解密方向、batch/dim 转发、单次启动、stream 同步及错误返回；覆盖零长度、容量、空指针、地址溢出和缓冲区重叠 |
| aice_cpu | CPU 加解密往返、原地一致性、参数检查、IV96 回归向量及 lane 编号高 16 位 |
| aice_cpu_kat | CPU 输出与固定 KAT 答案逐字节比较 |
| aice_host_only_build | 无 CANN SDK 的 Host 配置和编译 |
| aice_shared_launch | 链接生产共享库，检查公开符号、零长度同步、参数错误和同步错误返回 |

`test_public_header.c` 在构建时验证公开头文件的 C 语言兼容性。
`aice_host` 使用 ACL 和启动函数替身；`aice_shared_launch` 使用共享库及同步函数替身，
随设备构建生成。

## 设备测试

| 测试项 | 测试内容 |
| --- | --- |
| aice_device | NPU 加解密往返，以及原地、非原地结果一致性 |
| aice_cross | 同一输入的 NPU 密文与 CPU 密文逐字节比较；NPU 解密 CPU 密文后与原始明文比较 |
| aice_tail_regression | CPU 对照下的短输入、tile 边界、奇数字节尾部及多 tile 原地加解密 |
| aice_device_kat | 通过公开 C ABI 执行 NPU 加解密，与固定 KAT 答案逐字节比较 |

功能与交叉测试覆盖 batch=128/256/512、单 block、多 block、设备 AIV 核数对应的 dim，
以及原地和非原地处理。数据长度覆盖短输入、tile 边界和连续多个 tile；
分块覆盖整除、不整除、空闲 block 和每个 block 连续处理多个 tile。
lane 编号测试跨越 16 位符号边界和 65536 边界。

## CPU 对照实现

`assets/aice_cpu.c` 和 `assets/aice_api_cpu.h` 为 AICE submission 的 CPU 实现，
支持 x86 SSE2/AVX2 和 ARM NEON。

`aice_cpu_batch.c/.h` 按 batch=128/256/512 提供测试入口，
使用与 NPU 相同的连续 tile 分块、全局 lane 编号和尾块补零规则。
CPU 对照实现同时用于交叉测试和 benchmark 的结果校验。

## 固定答案测试

`assets/aice_kat.rsp` 来自 AICE submission，包含 7 组 KEYSTREAM 和 22 组 ENCRYPT 答案。
每组指定 key、IV、lane 及预期结果，分别验证三种 batch 的加密与解密。

KAT 是单 lane 数据。测试将第 w 个 16 位字放入 SoA 缓冲区的
`w * batch + lane` 位置，其他 lane 输入填零，比较指定 lane 的输出。
KEYSTREAM 组以全零明文检查密钥流；ENCRYPT 组使用文件中的 PT、CT 检查两个方向。
CPU KAT 和设备 KAT 分别读取同一份固定答案。

## 冒烟与尾块范围

`test_aice_device` 的 smoke 模式覆盖每种 batch 的 17 字节输入和多 tile 原地处理；
tail-regression 模式增加短输入及 tile 边界组合。
`test_aice_device_kat` 的 smoke 模式执行首组固定答案。
