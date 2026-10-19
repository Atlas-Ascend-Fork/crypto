# api — Public C ABI Implementation

English | [简体中文](README.md)

Implements the interfaces declared in `include/cann_crypto/`.

Responsibilities:

- Validate the ABI boundary: null pointers, `structSize` / `version`, handle validity, out-of-range enum values, and overlapping input/output buffers.
- Manage handle and plan lifecycles.
- Dispatch to `../algorithms/` according to the algorithm enum.

Algorithm-specific parameter checks, such as key, nonce, and tag lengths, belong in each algorithm's `op_host/` and are not duplicated here.
