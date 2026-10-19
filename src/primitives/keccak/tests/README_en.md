# Keccak Tests

English | [简体中文](README.md)

| Test | Coverage | Dependencies |
| --- | --- | --- |
| keccak_launch_config_test | Batch boundaries and core scheduling | C++17 |
| crypto_direct_launch_test | Parameter validation, scalar forwarding, and runtime and synchronization error propagation through the production Host entry point | ACL mock; no device required |
| crypto_public_headers_test | Public interfaces compile as pure C | C11 |
| keccak_golden / keccak_reference_kat | Known answers and the pure C reference implementation | Python / C11 |
| keccak_permutation_model | Vector permutation arithmetic model compared with the pure C reference | C++17 |
| keccak_build_tools | Host-only configuration, Kernel UT timeouts, and GTest ABI checks | CMake / Python |
| keccak_kernel_ut | CPU simulation of the actual kernel compared with an independent reference | GTest, tikicpulib |
| st/test_keccak.cpp | Byte-for-byte comparison of direct kernel results with the pure C reference | CANN, A2 or A3 |

Device test batch sizes: 1, 7, 8, 9, 64, 255, 256, 257, 1024, 4096, 8191, and 8192, including regression coverage for unaligned DMA tails.
CPU Kernel UT batch sizes: 1, 7, 8, 64, 255, 256, 257, and 320.

See [build_en.md](../docs/build_en.md) for build commands. Host mocks do not execute device kernels; device correctness must be verified separately in a Linux CANN environment.
