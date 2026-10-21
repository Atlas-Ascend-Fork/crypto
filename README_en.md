<div align="center">

# crypto

English | [简体中文](README.md)

[![license](https://img.shields.io/badge/license-CANN_Open_2.0-blue.svg)](LICENSE)
[![contributing](https://img.shields.io/badge/Contributing-teal)](CONTRIBUTING_en.md)
[![SIG](https://img.shields.io/badge/SIG-crypto-yellow)](https://gitcode.com/cann/community/blob/master/CANN/sigs/crypto/README.md)

</div>

---

## 🔥 Latest News

- [2026/09/15] Merged the `NTT` implementation, including transform paths for general `RNS`, `ML-KEM`, and `ML-DSA`.
- [2026/09/15] Merged `Keccak-f[1600]` to perform 24 rounds of permutation on batches of 1600-bit states.
- [2026/09/15] Merged `SHAKE128/256` to produce caller-specified output lengths for batches of equal-length messages.
- [2026/09/11] Merged `AES-CTR` and `AES-GCM` encryption implementations.
- [2026/07/27] Initialized the crypto repository.

## 🚀 Overview

crypto is the cryptographic operator and algorithm library for [CANN](https://hiascend.com/software/cann) (Compute Architecture for Neural Networks). It provides reusable cryptographic capabilities for the Ascend ecosystem.

The project focuses on two scenarios that CPUs do not cover well: protecting data already resident on an NPU without moving it back to the host, and executing cryptographic workloads on an NPU when a CPU cannot handle the required scale.

- The project plans to cover Chinese cryptographic algorithms (SM2, SM3, SM4, SM9, and ZUC) and widely used international algorithms (AES, ChaCha20, SHA-2/3, X25519, Ed25519, ML-KEM, and ML-DSA). The repository currently includes AES, Keccak-f, SHAKE, and NTT components. For other algorithms, refer to the corresponding source and component documentation; they should not be treated as delivered capabilities.
- Designed for batch processing and device-resident data; Device Kernels are implemented in Ascend C.
- Organized into three layers: mathematical engine, cryptographic primitives, and algorithm services. Lower-layer capabilities can be reused across algorithm families.

## 📌 Version Compatibility

This project's source releases follow CANN software releases. See the corresponding release notes in the [release-management repository](https://gitcode.com/cann/release-management) for the mapping between CANN software versions and this project's source tags.

To customize the source successfully, select the matching CANN version and GitCode source tag. The `master` branch may not match an installed CANN version.

## 🛠️ Prerequisites

Install the NPU driver and CANN packages before using the project. Some components depend on `torch` and `torch_npu`; see each component's `requirements.txt` for its exact dependencies.

The community also provides the [CANNLab online environment](https://gitcode.com/org/cann/cannlab/environment), which includes a matching CANN installation.

## ⬇️ Get the Source

```bash
# General form: git clone -b ${tag_version} https://gitcode.com/cann/crypto.git
git clone https://gitcode.com/cann/crypto.git
```

> If the source for the matching branch is already available in your environment, you can skip this step.

## 🔨 Build and Validate

The repository's `build.sh` is the unified entry point for builds and tests.

```bash
# Build all components
bash build.sh --pkg

# Build one component
bash build.sh --components=primitives/AES --pkg

# Run unit tests
bash build.sh -u --ophost      # op_host layer; no Ascend hardware required
bash build.sh -u --opapi       # op_api layer; no Ascend hardware required
bash build.sh -u --opkernel    # CPU Kernel UT; requires CANN and ABI-compatible GTest
bash build.sh -u               # All unit-test layers
bash build.sh --st             # Device system tests; requires Ascend hardware

# Show all options
bash build.sh -h
```

Components are discovered automatically by recursively searching `src/` for component directories containing `CMakeLists.txt` or `setup.py`. Nested build directories already belonging to an upper-level component are skipped; components may be located at different depths, such as `src/algorithms/hash/shake/`. Adding a component does not require changing the build script.

See [Component Organization](docs/component_layout_en.md) for Host/Kernel responsibilities, test categories, and optional framework adapters.

## 📂 Repository Layout

```text
crypto/
├── include/cann_crypto/   # Stable public C ABI; the only interface boundary with a compatibility promise
├── src/
│   ├── api/               # C ABI implementation: boundary validation, handles, plan lifecycle, algorithm dispatch
│   ├── core/              # Runtime: handles, streams, workspaces, dispatch, and key management
│   ├── math/              # Mathematical engine: big integers, modular arithmetic, NTT/RNS, GF(2^k), bit-plane transposition
│   ├── primitives/        # Cryptographic primitives: AES, SM4, Keccak-f, ChaCha, GHASH, Poly1305, EC
│   └── algorithms/        # Algorithm services: digest, AEAD, MAC, KDF, signature, KEM
├── reference/             # Pure C references, independent of CANN, used as correctness baselines for differential tests
├── tests/ut/              # Repository-level tests organized by common, op_api, op_host, and op_kernel
├── benchmarks/            # Benchmarks
├── examples/              # Invocation examples
├── scripts/               # Test-vector retrieval, dependency checks, and other utility scripts
├── docs/                  # Design and interface documentation
└── build.sh               # Unified build and test entry point
```

Dependencies flow only downward: `algorithms/` and `primitives/` do not implement mathematical operations themselves and instead use `math/`; algorithm families do not depend on one another. Each directory's README describes its responsibilities and constraints.

## 📖 Guides

- [Contributing guide](CONTRIBUTING_en.md): contribution workflow, code placement, deliverables, and review rules.
- [Documentation index](docs/README_en.md): design and interface documentation.

## 💬 More Information

- [Contributing guide](CONTRIBUTING_en.md)
- [Security statement](SECURITY_en.md)
- [License](LICENSE)
- [SIG](https://gitcode.com/cann/community/tree/master/CANN/sigs/crypto)
- [Committer list](https://gitcode.com/cann/community/blob/master/CANN/sigs/crypto/README.md#committer%E5%88%97%E8%A1%A8)

### Crypto SIG Discussion Group

Scan the QR code to contact the group administrator and request to join the Crypto SIG discussion group for community discussions and collaboration.

<img src="docs/images/wechat-group.png" alt="QR code for the Crypto SIG WeChat group administrator" width="260" />

---

This project's functionality and documentation are being updated continually. Follow the latest release for changes.

- **Report problems:** open an [Issue](https://gitcode.com/cann/crypto/issues) on GitCode.
- **Community discussion:** join the crypto SIG [monthly meeting](https://gitcode.com/cann/community/tree/master/CANN/sigs/crypto) or discuss topics in [Issues](https://gitcode.com/cann/crypto/issues).
