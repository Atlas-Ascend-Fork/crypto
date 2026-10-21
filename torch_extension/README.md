# PyTorch / torch_npu 适配

简体中文 | [English](README_en.md)

此顶层目录提供可选的 PyTorch 适配，可单独构建，当前通过仓库中的[原生 AES 核心](../src/primitives/AES/README.md)适配 AES-128-CTR/GCM。核心不依赖 PyTorch；适配层复用原生实现，按实际需要增加绑定。

- `ascend_ops/`：Python 导入入口。
- `csrc/`：Tensor 连续化、设备 guard、当前 NPU stream、Meta 和 `TORCH_LIBRARY` 注册。CTR/GCM 共用 `aes_binding.cpp`；绑定文件较多时再按算子拆分子目录。
- `tests/ctr/`、`tests/gcm/`：原有 35 项 PyTorch 接口及设备回归。
- `tests/ut/`：依赖 Torch 和 OpenSSL 开发头文件的 AAD Host 适配测试。
- `examples/`：原有 CTR/GCM Python 调用样例。

`import ascend_ops` 后仍使用 `torch.ops.ascend_ops.aes128ctr(input, key, nonce)` 和 `torch.ops.ascend_ops.aes_gcm(input, key, nonce, aad)`。输入输出布局及已知 GCM 长度约束保持；进入原生核心前刷新 Torch 任务队列，计算完成后返回，临时 Tensor 和 ACL 缓冲区不会提前释放。

## 构建与验证

先加载 CANN 环境并安装版本匹配的 PyTorch、torch_npu，再从仓库根目录运行：

```bash
cd torch_extension
python3 -m pip install -r requirements.txt
python3 setup.py bdist_wheel
python3 -m pip install dist/*.whl --force-reinstall --no-deps
python3 -m pytest tests/ctr tests/gcm -v
python3 -m pytest tests/ut -v
```

CMake 自动构建同仓 AES 核心，并将 `_C.abi3.so`、`libcann_crypto_aes.so` 一起打入 `ascend_ops` wheel。扩展通过 `$ORIGIN` 查找同目录核心库，安装后不依赖源码或原构建目录；CANN 及 Torch/torch_npu 运行时仍须由环境提供。`NPU_ARCH` 默认 `dav-2201`，不宣称新增硬件兼容性。

`build_and_test.sh` 保留安装依赖、构建/安装 wheel 和执行测试的一体入口。核心原生 Host UT 与设备 ST 从 AES 组件 CMake 独立运行。
