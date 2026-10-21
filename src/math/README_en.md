# math — Mathematical Engine

English | [简体中文](README.md)

Mathematical implementations shared by all algorithm families.

Available component: [NTT](ntt/README_en.md).

Scope:

- Big integers and modular arithmetic (Montgomery and Barrett).
- NTT / INTT / RNS.
- GF(2^k).
- Bit-plane transposition.
- Integer convolution with limited bit widths for Cube units.

Constraints:

- This directory has a single owner.
- No dependencies on types specific to an algorithm.
- `primitives/` and `algorithms/` must not reimplement operations already covered by this directory.
