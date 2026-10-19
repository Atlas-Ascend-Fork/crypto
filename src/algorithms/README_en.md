# algorithms — Algorithm Services

English | [简体中文](README.md)

Complete cryptographic services, organized by algorithm family: `digest` / `aead` / `mac` / `kdf` / `signature` / `key_exchange` / `kem`.

Each algorithm directory follows the CANN operator repository layout:

```text
<alg>/
├── <alg>_internal.h    # Internal interface with neutral types, shared by the native API and aclnn entry points
├── <alg>_launch.cpp    # Kernel composition and variant selection
├── op_api/             # Two-stage aclnn entry points (optional; absence means direct aclnn calls are not yet supported)
├── op_host/            # Operator definition, InferShape, and Tiling
├── op_kernel/          # Ascend C implementation
├── tests/
└── CMakeLists.txt
```

Constraints:

- Compose operations, validate parameters, and schedule execution; do not implement mathematical operations.
- Algorithm families must not depend on one another. Shared capabilities belong in `../primitives/` or `../math/`.
- Tiling interfaces in `op_host/` accept only neutral scalar types so that the native API and aclnn entry points can share one implementation.
