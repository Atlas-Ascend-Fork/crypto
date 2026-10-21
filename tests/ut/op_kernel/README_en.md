# op_kernel

English | [简体中文](README.md)

Ascend C kernel tests: computational correctness, boundary and tail handling, and multicore scheduling.

CPU Kernel UT uses CANN tikicpulib and ABI-compatible GTest without Ascend hardware. End-to-end device tests reside in component `tests/st/` directories and run through `build.sh --st`.

They run alongside tests at the same layer under each component's `src/<component>/tests/ut/op_kernel/` through `build.sh -u --opkernel`.
