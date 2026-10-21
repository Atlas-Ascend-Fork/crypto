# PyTorch / torch_npu Integration

English | [简体中文](README.md)

This top-level directory provides optional PyTorch adapters with a separate build entry point. It currently adapts AES-128-CTR/GCM through the repository's [native AES core](../src/primitives/AES/README_en.md). The core does not depend on PyTorch; adapters reuse native implementations, and bindings are added as needed.

- `ascend_ops/`: Python import entry point.
- `csrc/`: Tensor materialization, device guards, the current NPU stream, Meta implementations, and `TORCH_LIBRARY` registration. CTR/GCM share `aes_binding.cpp`; add per-operator subdirectories when there are enough binding files to need them.
- `tests/ctr/`, `tests/gcm/`: the original 35 PyTorch interface and device regression cases.
- `tests/ut/`: AAD Host adapter tests requiring Torch and OpenSSL development headers.
- `examples/`: existing CTR/GCM Python invocation examples.

After `import ascend_ops`, continue calling `torch.ops.ascend_ops.aes128ctr(input, key, nonce)` and `torch.ops.ascend_ops.aes_gcm(input, key, nonce, aad)`. Input/output layouts and the known GCM length constraints remain unchanged. The adapter flushes the Torch task queue before entering the native core and returns after computation, keeping temporary Tensors and ACL buffers alive until completion.

## Build and validation

Load the CANN environment and install matching PyTorch and torch_npu versions, then run from the repository root:

```bash
cd torch_extension
python3 -m pip install -r requirements.txt
python3 setup.py bdist_wheel
python3 -m pip install dist/*.whl --force-reinstall --no-deps
python3 -m pytest tests/ctr tests/gcm -v
python3 -m pytest tests/ut -v
```

CMake builds the repository's AES core and bundles both `_C.abi3.so` and `libcann_crypto_aes.so` in the `ascend_ops` wheel. The extension locates the adjacent core library through `$ORIGIN`; an installed wheel does not need the source or original build directory. CANN and Torch/torch_npu runtimes must still be provided by the environment. `NPU_ARCH` defaults to `dav-2201`; no new hardware compatibility is claimed.

`build_and_test.sh` retains the combined dependency installation, wheel build/installation, and test entry point. Native Host UT and device ST run independently through the AES component CMake build.
