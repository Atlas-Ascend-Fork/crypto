# Tests

English | [简体中文](README.md)

The directory layout follows `ops-math`, `ops-nn`, and `ops-transformer`: repository-level tests live under
`ut/`, grouped by operator code layer. Component tests live under each component's `tests/ut/`
and follow the same layering.

| Directory | Contents | Ascend Hardware Required |
| --- | --- | --- |
| [`ut/common/`](ut/common/README_en.md) | Tests shared across algorithms: KAT vector comparisons, differential tests against `reference/`, and ABI compatibility checks | No |
| [`ut/op_api/`](ut/op_api/README_en.md) | aclnn interface layer | No |
| [`ut/op_host/`](ut/op_host/README_en.md) | Host logic such as Tiling and InferShape | No |
| [`ut/op_kernel/`](ut/op_kernel/README_en.md) | Ascend C kernels | Yes |

Run tests with:

```bash
bash build.sh -u                # All layers
bash build.sh -u --ophost       # op_host layer only
bash build.sh -u --opkernel     # op_kernel layer only
```

Each gate run has a 15-minute limit, so only layers that do not require a device run in the gate.
The full `op_kernel` matrix and performance regressions belong in daily builds.

Scripts for retrieving NIST ACVP, Wycheproof, and GM/T standard vectors and verifying their hashes are still planned; see [Utility Scripts](../scripts/README_en.md). Existing component tests use reference implementations and known answers shipped with the source, such as the [Keccak tests](../src/primitives/keccak/tests/README_en.md) and [SHAKE tests](../src/algorithms/hash/shake/tests/README_en.md).
