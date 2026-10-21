# NTT

English | [简体中文](README.md)

The NTT component uses native Ascend C kernel launch. The Host prepares parameters and Tiling and launches the device kernel. Generic NTT, four-step, and PQC implementations remain split across device source files. This directory layout adds neither ACLNN registration nor a PyTorch dependency.

## Layout

| Directory | Responsibility |
| --- | --- |
| `op_host/` | Parameter and buffer checks, Tiling, runtime launch, internal C++ interface, and data layout utilities |
| `op_kernel/` | Kernel entry, Cube/Vector computation, PQC paths, stage scheduling, and shared Tiling data |
| `tests/ut/op_host/` | Host, data generator, and command-line regressions without CANN |
| `tests/ut/op_kernel/` | CPU unit tests executing the actual kernel with tikicpulib |
| `tests/st/` | Device end-to-end correctness test entry through the native interface |
| `tests/assets/` | Fixed golden digests for the data generators |
| `examples/` | Device memory, stream management, and native API example |
| `scripts/` | Data generation, comparison, and performance acceptance scripts shared by examples and tests |
| `cmake/` | Device build, installation, and packaging |

`op_kernel/ntt.cpp` is a thin entry point. Implementations remain separated by responsibility: `ntt_device_pqc.hpp` handles PQC paths, `ntt_device_subntt.hpp` handles sub-NTTs, `ntt_device_phase2.hpp` handles the second four-step stage, and `ntt_device_merge.hpp` handles merging. These implementations do not need to be concatenated into one file.

## Build and use

Run the following commands from the repository root. The Host library and tests require only a C++17 compiler, CMake 3.16+, and Python 3.9+:

```bash
cmake -S src/math/ntt -B build/ntt-host -DBUILD_TESTING=ON
cmake --build build/ntt-host -j 4
ctest --test-dir build/ntt-host -L host_ut --output-on-failure
```

Device builds require an installed CANN SDK and a concrete chip model, such as `Ascend910B3` on A2:

```bash
bash src/math/ntt/run.sh --stage build -r npu -v Ascend910B3 -i /path/to/cann
```

The Host implementation builds as `cann_crypto_ntt`, the device launch wrapper as `cann_crypto_ntt_device`, and the kernel as `ntt_kernels`. `op_kernel/ntt.cpp` continues to build through `ascendc_library`; the directory migration does not require changing its suffix. The example uses the native wrapper for `ACLRT_LAUNCH_KERNEL` to launch the kernel.

`NttParams`, `NttBuffers`, and `LaunchNtt` in `op_host/ntt_internal.h` remain an internal C++17 interface, not a stable public C ABI. The caller owns the device buffers and stream. Launch does not synchronize; buffers must remain valid until the stream completes. The installed internal header remains at `include/cann_crypto/ntt/ntt_internal.h`.

See the [test guide](tests/README_en.md) for complete validation commands and dependencies. Performance acceptance continues to use `scripts/kem_entry.sh`, `scripts/dsa_entry.sh`, and `scripts/rns_entry.sh`; `scripts/run_npu_server.sh` preserves existing callers.
