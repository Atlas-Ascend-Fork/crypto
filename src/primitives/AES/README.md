# AES Primitives

This component provides two AES-128 NPU operators implemented with Ascend C and exposed through the `ascend_ops` PyTorch extension.

## Operators

| Operator | PyTorch call | Implementation |
| --- | --- | --- |
| AES-128-CTR | `torch.ops.ascend_ops.aes128ctr(input, key, nonce)` | `csrc/ctr/dav-2201/ctr.asc` |
| AES-128-GCM | `torch.ops.ascend_ops.aes_gcm(input, key, nonce, aad)` | `csrc/gcm/dav-2201/gcm.asc` |

The operator schemas above are registered by the implementation files. The operator-specific READMEs document their input constraints and output layout:

- [AES-128-CTR](csrc/ctr/dav-2201/README.md)
- [AES-128-GCM](csrc/gcm/dav-2201/README.md)

## Component layout

- `csrc/`: Ascend C kernels and PyTorch operator registration.
- `examples/`: runnable CTR and GCM examples, including the shared AES-128 key expansion helper.
- `tests/`: PyTorch tests for the CTR and GCM operators.
- `setup.py`: CMake-backed wheel build.
- `build_and_test.sh`: dependency installation, wheel build, installation, and pytest entry point.
- `requirements.txt`: Python build and test dependencies.

## Prerequisites

Use a configured CANN environment with Python 3.8 or newer, PyTorch, and the matching `torch_npu` package. Install the additional Python dependencies from `requirements.txt`.

The checked-in operator build files are under `csrc/*/dav-2201`; the component CMake configuration uses `dav-2201` by default.

## Build and test

From the repository root, build and install this component with:

```bash
bash build.sh --components=primitives/AES --pkg
```

After installation, run its tests with:

```bash
bash build.sh --components=primitives/AES -u --opkernel
```

Alternatively, from this component directory run the provided entry point:

```bash
cd src/primitives/AES
bash build_and_test.sh
```

The component entry point runs `pytest tests/ -v` after building and installing the wheel.

## Examples

After building and installing the extension, run the examples from their directory:

```bash
cd src/primitives/AES/examples
python3 aes128_ctr_example.py
python3 aes128_gcm_example.py
```

Both scripts accept an optional device ID and validate the result against a known answer. `aes_key_expansion.py` expands a 16-byte AES-128 key into the round-key format consumed by the operators.

The corresponding tests are [tests/ctr/test_ctr.py](tests/ctr/test_ctr.py) and [tests/gcm/test_gcm.py](tests/gcm/test_gcm.py).
