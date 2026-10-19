# primitives — Cryptographic Primitives

English | [简体中文](README.md)

Algorithm kernel implementations: AES, SM4, Keccak-f, ChaCha, GHASH, Poly1305, EC point operations, etc.

Constraints:

- These primitives do not provide complete security services on their own (for example, GHASH alone is not a MAC), so they are not exposed to end users by default.
- Do not implement mathematical operations; place them in `../math/` when needed.
- Algorithm families must not depend on one another through this directory.
