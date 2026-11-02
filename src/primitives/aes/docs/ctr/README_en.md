# AES-128-CTR

English | [简体中文](README.md)

## Supported Products

| Product | Supported |
| ---- | :----:|
|Atlas A2 training series / Atlas A2 inference series|√|

## Function

- Operator function: perform AES-128-CTR stream encryption.

- Formula:

$$
\text{output} = \text{AES-128-CTR}(\text{input}, \text{key}, \text{nonce})
$$

## Parameters

| Parameter | Input / Output / Attribute | Description | Data Type | Format |
| --- | --- | --- | --- | --- |
| input | Input | Plaintext input for AES-128-CTR encryption. | UINT8 | ND |
| key | Input | Pre-expanded AES-128 round keys: 176 bytes for 11 rounds (44 uint32_t values). | UINT8 | ND |
| nonce | Input | 12-byte nonce (IV) used to construct the initial CTR counter value. | UINT8 | ND |
| output | Output | Ciphertext output of AES-128-CTR encryption, with its length rounded up to a multiple of 512 bytes. | UINT8 | ND |

## Constraints

1. The `input` length must be greater than 0 and no more than 4,294,966,784 bytes so that the length, rounded up to a multiple of 512 bytes, fits in the 32-bit device metadata.
2. The `key` input must contain at least 176 bytes, including all 11 rounds of expanded AES round keys.
3. The `nonce` input must contain at least 12 bytes.

## Contributions

| Contributor | Organization | Operator | Date | Contribution |
| ---- | ---- | ---- | ---- | ---- |
| 汤佳琪 | Prof. Hongzhi Zhao's team, Beijing Jiaotong University | AES-128-CTR | 2026/06/16 | Adapted the AES-128-CTR operator for the open-source repository |
| 范志鹏 | Prof. Hongzhi Zhao's team, Beijing Jiaotong University | AES-128-CTR | 2026/06/16 | Adapted the AES-128-CTR operator for the open-source repository |
| 韩智惟 | Prof. Hongzhi Zhao's team, Beijing Jiaotong University | AES-128-CTR | 2026/06/16 | Adapted the AES-128-CTR operator for the open-source repository |
|  张昕  | Prof. Hongzhi Zhao's team, Beijing Jiaotong University | AES-128-CTR | 2026/06/16 | Adapted the AES-128-CTR operator for the open-source repository |
