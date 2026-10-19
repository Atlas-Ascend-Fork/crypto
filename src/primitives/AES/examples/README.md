# AES 调用样例

简体中文 | [English](README_en.md)

两个样例都在 NPU 上加密一组已知答案并自行校验。通过时打印 PASS 并返回 0，不通过时打印期望值与实际值并返回 1。

| 样例 | 算子 | 已知答案来源 |
| --- | --- | --- |
| `aes128_ctr_example.py` | `torch.ops.ascend_ops.aes128ctr` | RFC 3686 测试向量 #2 |
| `aes128_gcm_example.py` | `torch.ops.ascend_ops.aes_gcm` | 密钥、nonce 与 AAD 取自 GCM 规范测试用例 4，明文为 2560 字节；期望值由 pycryptodome 与 pyca/cryptography 分别计算并核对一致 |

`aes_key_expansion.py` 按 FIPS 197 把 16 字节密钥扩展为算子所需的轮密钥，两个样例共用。

## 运行

先按[组件 README](../README.md) 构建并安装 `ascend_ops`，加载 CANN 环境后在本目录执行：

```bash
python3 aes128_ctr_example.py      # 默认使用 0 号设备
python3 aes128_gcm_example.py 1    # 指定设备号
```

## 调用要点

- 密钥参数是 11 轮扩展后的 176 字节轮密钥，不是 16 字节原始密钥。
- nonce 为 12 字节。CTR 的计数器从 1 开始，GCM 加密的计数器从 2 开始，与标准一致。
- 输出长度按 512 字节补齐。CTR 输出的前 `len(plaintext)` 字节是密文；GCM 在补齐后的密文之后附 16 字节标签。
- 当前实现的 GCM 标签只在明文长度为 2560 字节整数倍时正确：输入按 512 字节补齐后分给 20 个核计算 GHASH，块数不能被 20 整除时合并出的标签有误，密文不受影响。
- GCM 的 AAD 最多 32 字节。
