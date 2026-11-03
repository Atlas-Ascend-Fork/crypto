# SHAKE Tests

English | [简体中文](README.md)

- Host tests cover launch parameters, division-by-zero protection, size overflow, group scheduling, the sponge model, and an independent pure C reference implementation.
- `shake_direct_launch_test` verifies null pointers, buffer overlap, scalar arguments, and ACL error propagation through the public direct-launch entry points.
- CPU Kernel UT uses the actual SHAKE128 and SHAKE256 kernels, with four parameterized cases for each.
- `st/test_shake.cpp` uses OpenSSL EVP to verify 18 parameter sets each for SHAKE128 and SHAKE256, plus 12 Keccak batch sizes.
- Device cases cover empty messages, unaligned lengths, rate boundaries, multi-block absorption, and output expansion.
- `stress.py` repeatedly runs two representative parameter sets for each algorithm per iteration, saving logs in the build directory.

See [build_en.md](../docs/build_en.md) for commands. Host tests do not require CANN; CPU Kernel UT requires GTest with a matching ABI and tikicpulib.

`ut/op_host/` checks Host logic. `ut/op_kernel/` contains lightweight model tests and CPU simulation of the actual kernels, which requires explicitly enabling `CANN_BUILD_CPU_DEBUG_TESTS=ON`. The latter runs on the CPU under the CTest label `kernel_ut`. Tests in `st/` exercise the complete public API path on a real NPU under the label `device`, with `CANN_BUILD_DEVICE=ON`. Passing CPU simulation does not replace device ST.
