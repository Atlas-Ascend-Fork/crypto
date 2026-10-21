# Documentation Index

English | [简体中文](README.md)

| Document | Description | Status |
| --- | --- | --- |
| `architecture.md` | Layered architecture, API design, and repository organization | Under SIG review; not yet merged |

- Documentation closely tied to code, such as algorithm interfaces and examples, is stored alongside the corresponding code.

## Component Documentation

| Component | Usage and Interfaces | Tests and Examples |
| --- | --- | --- |
| AES | [Overview](../src/primitives/AES/README_en.md), [CTR](../src/primitives/AES/docs/ctr/README_en.md), [GCM](../src/primitives/AES/docs/gcm/README_en.md) | [Native example](../src/primitives/AES/examples/README_en.md), [PyTorch examples](../torch_extension/examples/README_en.md) |
| Keccak | [Overview](../src/primitives/keccak/README_en.md), [Build and Run](../src/primitives/keccak/docs/build_en.md), [Direct kernel interface](../src/primitives/keccak/docs/kernelLaunch_en.md) | [Tests](../src/primitives/keccak/tests/README_en.md) |
| SHAKE | [Overview](../src/algorithms/hash/shake/README_en.md), [Build and Run](../src/algorithms/hash/shake/docs/build_en.md), [Direct kernel interfaces](../src/algorithms/hash/shake/docs/kernelLaunch_en.md) | [Tests](../src/algorithms/hash/shake/tests/README_en.md) |

## Repository Directory Guides

- Interfaces and layers: [public interfaces](../include/cann_crypto/README_en.md), [C ABI implementation](../src/api/README_en.md), [runtime](../src/core/README_en.md), [mathematical engine](../src/math/README_en.md), [cryptographic primitives](../src/primitives/README_en.md), and [algorithm services](../src/algorithms/README_en.md).
- Development and validation: [tests](../tests/README_en.md), [benchmarks](../benchmarks/README_en.md), [reference implementations](../reference/README_en.md), [examples](../examples/README_en.md), and [utility scripts](../scripts/README_en.md).
