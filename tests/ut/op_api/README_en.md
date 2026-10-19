# op_api

English | [简体中文](README.md)

Tests for the aclnn interface layer: parameter validation, workspace calculation, and executor lifecycles.

Ascend hardware required: no. These tests belong in the gate.

They run alongside tests at the same layer under each component's `src/<component>/tests/ut/op_api/` through `build.sh -u --opapi`.
