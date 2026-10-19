# Utility Scripts

English | [简体中文](README.md)

Scripts supporting builds, test-vector retrieval, code checks, and related workflows.

- **`ci/`**: the build script `compile.sh`, unit-test script `ut.sh`, and NPU smoke-test script `smoke.sh` invoked by the gate pipeline. These are reviewed by the SIG and will move to `.gitcode/scripts/` once stable.

Planned scripts:

- **Test-vector retrieval**: download NIST ACVP, Wycheproof, and GM/T standard vectors and verify their hashes. Vector files are not checked into the repository.
- **Layer dependency checks**: ensure that `algorithms/` and `primitives/` do not bypass `math/` by implementing mathematical operations themselves.
- **Build artifact dependency checks**: ensure that the core library does not link against third-party dependencies of the adapter layer.
