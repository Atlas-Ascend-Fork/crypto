# op_host

English | [简体中文](README.md)

Host tests: Tiling calculations, InferShape inference, and operator registration.

Ascend hardware required: no. These tests belong in the gate.

They run alongside tests at the same layer under each component's `src/<component>/tests/ut/op_host/` through `build.sh -u --ophost`.
