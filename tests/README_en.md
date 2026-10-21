# Tests

English | [简体中文](README.md)

UT/ST describe test scope; Host/Kernel describe the code layer under test.

| Location | Coverage | Environment |
| --- | --- | --- |
| [`tests/ut/common/`](ut/common/README_en.md) | Repository-wide regression tests | Dependencies of each test |
| Component [`tests/ut/op_host/`](ut/op_host/README_en.md) | Validation, tiling, scheduling, runtime mocks | C/C++, Python; no device |
| Component [`tests/ut/op_kernel/`](ut/op_kernel/README_en.md) | CPU debugging of kernel computation | CANN tikicpulib and ABI-compatible GTest; no device |
| Component `tests/st/` | Public API through real kernels, compared with golden results | CANN and Ascend hardware |
| Component `tests/assets/` | Independent reference implementations, fixed vectors and data | Used by tests |

```bash
bash build.sh -u --ophost       # Host UT
bash build.sh -u --opkernel     # CPU Kernel UT
bash build.sh --st             # Device ST
bash build.sh -u               # All unit-test layers, excluding ST
```

For CTest components, Host runs exclude the `device` and `kernel_ut` labels.
CPU Kernel UT enables `CANN_BUILD_CPU_DEBUG_TESTS=ON` and selects `kernel_ut`;
device ST selects `device`. NTT retains its component script for device validation.
Missing CANN dependencies for an explicitly requested test return failure.
The default per-test timeout is 1800 seconds to accommodate CPU Kernel simulation; override it with `--ut_timeout=<seconds>`.

`--allow_empty` only permits a request with no executable test entry points discovered; zero tests after CTest selection, build failures, and test failures still return failure.

PR Host checks, CPU Kernel UT, and device ST run separately. Host mocks and CPU
models do not replace device validation of DMA, synchronization, and numerical results.
The device smoke entry point is `scripts/ci/smoke.sh`. Framework tests reside with
the adapter and run separately according to its instructions.

Scripts to fetch NIST ACVP, Wycheproof, and GM/T vectors remain planned; see
[Scripts](../scripts/README_en.md). For shipped reference implementations and known
answers, see the [Keccak](../src/primitives/keccak/tests/README_en.md) and
[SHAKE](../src/algorithms/hash/shake/tests/README_en.md) test documentation.

For additional public-interface tests, see [op_api](ut/op_api/README_en.md).
