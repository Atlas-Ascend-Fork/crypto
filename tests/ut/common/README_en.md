# common — Tests Shared Across Algorithms

English | [简体中文](README.md)

Tests that do not belong to an individual operator live here:

- **KATs**: comparisons against NIST ACVP, Wycheproof, and GM/T standard vectors.
- **Differential tests**: comparisons against the pure C references in `../../../reference/`.
- **ABI compatibility**: structure sizes, field offsets, and exported symbols from public headers.
- **Negative cases**: incorrect tags, invalid lengths, zero counts, non-monotonic offsets, and overlapping buffers.

All these tests are independent of Ascend hardware and are included in the gate scope.
