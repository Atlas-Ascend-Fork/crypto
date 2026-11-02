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
cmake -S src/math/ntt -B build/ntt-host -DCANN_BUILD_TESTS=ON
cmake --build build/ntt-host -j 4
ctest --test-dir build/ntt-host -L host_ut --output-on-failure
```

Device builds use the native ASC CMake interface from CANN 9.0.0, as do AES, Keccak, and SHAKE. `ASCEND_COMPUTE_UNIT` defaults to `ascend910b` (A2); use `ascend910_93` for A3. Both compile for `dav-2201`, without a separate NTT `SOC_VERSION`.

```bash
source /path/to/cann-9.0.0/set_env.sh
ASCEND_COMPUTE_UNIT=ascend910b bash build.sh --pkg --components=math/ntt
ASCEND_COMPUTE_UNIT=ascend910b bash build.sh --st --components=math/ntt
# For A3, replace ascend910b with ascend910_93; AES, Keccak, and SHAKE can be selected together.
```

The standalone script accepts the same targets; `-v` overrides the environment:

```bash
bash src/math/ntt/run.sh --stage build -r npu -v ascend910b -i /path/to/cann-9.0.0
bash src/math/ntt/run.sh --stage env -v ascend910_93 -i /path/to/cann-9.0.0
```

`--stage env` prints the SDK, target family, and compiler architecture without building or querying a device. Existing concrete names such as `Ascend910B3` remain aliases for their families. Device tests check that the actual device matches the requested family. Local `sim` uses the Ascend910B1 model with `-r sim -v ascend910b`; the simulator model is separate from the device compilation target.

The Host library is `cann_crypto_ntt`. The device library `cann_crypto_ntt_device` launches the kernel through a `.asc` wrapper using `<<<...>>>`. `op_kernel/ntt.cpp` remains shared with CPU Kernel UT; `ascendc_library` and the generated `aclrtlaunch_ntt.h` are no longer used. Root device tests use CTest to run the already-built example for ML-KEM, ML-DSA, RNS, and launch error reporting.

`NttParams`, `NttBuffers`, and `LaunchNtt` in the source header [op_host/ntt_internal.h](op_host/ntt_internal.h) remain an internal C++17 interface, not a stable public C ABI. The caller owns the device buffers and stream. Launch does not synchronize; buffers must remain valid until the stream completes.

The device build's [installation rules](cmake/npu_lib.cmake) install this file to `<install-prefix>/include/cann_crypto/ntt/ntt_internal.h`. This path belongs to the installation directory; the repository's top-level `include/cann_crypto/` does not contain this internal header. Building only the Host tests does not install it.

The `run.sh` command above installs the device libraries and headers into `install/` under the directory printed as `[BUILD] directory`. To choose another installation location, replace `/path/to/ntt-device-build` below with that device build directory and run from the repository root:

```bash
cmake --install /path/to/ntt-device-build --prefix "$PWD/build/ntt-install"
ls build/ntt-install/include/cann_crypto/ntt/ntt_internal.h
```

See the [test guide](tests/README_en.md) for complete validation commands and dependencies. Performance acceptance continues to use `scripts/kem_entry.sh`, `scripts/dsa_entry.sh`, and `scripts/rns_entry.sh`; `scripts/run_npu_server.sh` preserves existing callers.
