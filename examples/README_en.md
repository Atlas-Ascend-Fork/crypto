# Examples

English | [简体中文](README.md)

Minimal runnable examples for users.

## Plan

- One minimal example per algorithm family: batch hashing, AEAD encryption and decryption, and KEM encapsulation and decapsulation.

## Constraints

- Examples must build independently, without depending on anything under `tests/`.
- Use only the public interfaces under `include/cann_crypto/`; do not include internal headers.
