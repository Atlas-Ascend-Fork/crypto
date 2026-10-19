# Reference Implementations

English | [简体中文](README.md)

Pure C implementations of the algorithms serve as correctness oracles for differential tests.

## Constraints

- **Favor obvious correctness; do not optimize.** Readability takes priority over performance.
- **No CANN dependencies.** The references must build independently as `libcann_crypto_ref.a` on a machine without Ascend hardware or an Ascend toolchain. Otherwise, KATs and differential tests cannot run in the PR gate and must be deferred to daily builds.
- **Do not include any files under `src/`.** A reference that shares code with the implementation under test is no longer an independent oracle. Only `include/cann_crypto/` and libc dependencies are allowed.

## Usage

- Differential tests in `tests/ut/common/` use this directory as their reference.
