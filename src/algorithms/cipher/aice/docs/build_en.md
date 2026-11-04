# AICE Build, Test, and Benchmark

English | [简体中文](build.md)

Run the following commands from the cann-crypto repository root. Host builds require CMake 3.16+, a C99 compiler, and a C++17 compiler; device builds require the CANN Toolkit, and device tests and benchmarks run on the corresponding Ascend hardware.

## Build

Load the CANN environment and select the target device:

```bash
source /usr/local/Ascend/ascend-toolkit/set_env.sh
export ASCEND_COMPUTE_UNIT=ascend910b
# use ascend950 for Ascend 950
bash build.sh --components=algorithms/cipher/aice --pkg
```

The default configuration is Release; artifacts are placed under `src/algorithms/cipher/aice/build/`:

| Artifact | Purpose |
| --- | --- |
| libcann_crypto_aice.so | AICE shared library |
| aice_example | Encryption and decryption example |
| bench/aice_bench | Encryption benchmark program |

Both targets compile `device_dav2201.h`; the target architecture selects the arithmetic implementation.

## Example

The example encrypts and decrypts 1024 bytes with batch=128 and dim=1, then checks that the plaintext is recovered. The argument is the ACL logical device number, defaulting to 0:

```bash
./src/algorithms/cipher/aice/build/aice_example 0
```

On success it prints `AICE_EXAMPLE=PASS`. See [aice_example.cpp](../examples/aice_example.cpp) for the source.

## Tests

Host unit tests:

```bash
bash build.sh --components=algorithms/cipher/aice -u --ophost
```

Device tests:

```bash
bash build.sh --components=algorithms/cipher/aice --st
```

The host build directory is `src/algorithms/cipher/aice/build/host`.
The device test build directory is `src/algorithms/cipher/aice/build/device-${ASCEND_COMPUTE_UNIT}`.
`--st` runs functional self-checks, CPU cross tests, tail tests, and device KATs; see [test coverage](../tests/README_en.md) for details.

After the device test build completes, you can run a single test or the smoke tests:

```bash
AICE_TEST_BUILD="src/algorithms/cipher/aice/build/device-${ASCEND_COMPUTE_UNIT}"
ctest --test-dir "$AICE_TEST_BUILD" -R '^aice_cross$' --output-on-failure
ctest --test-dir "$AICE_TEST_BUILD" -R '^aice_device_kat$' --output-on-failure
"$AICE_TEST_BUILD/bin/test_aice_device" --self-test --smoke
"$AICE_TEST_BUILD/bin/test_aice_device" --cross-test --smoke
"$AICE_TEST_BUILD/bin/test_aice_device_kat" --smoke
```

## Benchmark

Run the default size matrix directly after building:

```bash
./src/algorithms/cipher/aice/build/bench/aice_bench
```

The defaults are batch=512, dim=1, warmup=3, and iterations=10, testing
1, 4, 16, 64, 256 KiB and 1, 4, 16, 64 MiB in turn.

To specify a size and run parameters:

```bash
./src/algorithms/cipher/aice/build/bench/aice_bench \
  --size-bytes=1048576 --batch=512 --dim=1 --warmup=3 --iterations=100
```

| Option | Meaning | Default |
| --- | --- | --- |
| --size-bytes | Bytes encrypted per call; when set, only this size is measured | Nine-step size matrix |
| --batch | Lanes per block: 128, 256, 512 | 512 |
| --dim | Number of blocks launched | 1 |
| --warmup | Warmup runs, may be 0 | 3 |
| --iterations | Launches inside the timed window, at least 1 | 10 |

For each size the ciphertext is first verified against the CPU reference, then the benchmark warms up.
The kernel is launched iterations times asynchronously between two ACL events, and synchronization
happens after the closing event. Timing includes in-kernel state initialization, GM/UB transfers,
and the encryption computation; memory allocation, host/device copies, CPU verification, and warmup
are outside the timed window.

The reported `kernel_us` is the average per-kernel time derived from the event window, and `Gbps` is the decimal throughput:

```text
kernel_us = elapsed_ms * 1000 / iterations
Gbps = size_bytes * 8 / kernel_us / 1000
```

## CMake Configuration and Installation

Use the component CMake directly when you need a separate build directory, a device number, or an install prefix:

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

| Option | Meaning | Default |
| --- | --- | --- |
| CANN_BUILD_DEVICE | Build the device shared library | OFF |
| CANN_BUILD_TESTS | Build host tests and device ST | ON |
| CANN_BUILD_BENCHMARKS | Generate the benchmark in device builds | ON |
| ASCEND_COMPUTE_UNIT | ascend910b or ascend950 | ascend910b |
| ASCEND_CANN_PACKAGE_PATH | Path to the CANN Toolkit | ASCEND_HOME_PATH |
| CANN_DEVICE_ID | ACL logical device used by ST and the benchmark | 0 |

Host-only configuration:

```bash
cmake -S src/algorithms/cipher/aice -B build-aice-host \
  -DCMAKE_BUILD_TYPE=Release -DCANN_BUILD_DEVICE=OFF -DCANN_BUILD_TESTS=ON
cmake --build build-aice-host -j
ctest --test-dir build-aice-host --output-on-failure
```

The install step produces `lib/libcann_crypto_aice.so` and `include/cann_crypto/aice.h`.
Callers include the header and link with `-lcann_crypto_aice`; the matching CANN runtime is loaded at run time.
