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

<table style="table-layout: fixed; width: 980px"><colgroup>
  <col style="width: 100px">
  <col style="width: 150px">
  <col style="width: 280px">
  <col style="width: 330px">
  <col style="width: 120px">
  </colgroup>
  <thead>
    <tr>
      <th>Parameter</th>
      <th>Input / Output / Attribute</th>
      <th>Description</th>
      <th>Data Type</th>
      <th>Format</th>
    </tr></thead>
  <tbody>
    <tr>
      <td>input</td>
      <td>Input</td>
      <td>Plaintext input for AES-128-CTR encryption.</td>
      <td>UINT8</td>
      <td>ND</td>
    </tr>
    <tr>
      <td>key</td>
      <td>Input</td>
      <td>Pre-expanded AES-128 round keys: 176 bytes for 11 rounds (44 uint32_t values).</td>
      <td>UINT8</td>
      <td>ND</td>
    </tr>
    <tr>
      <td>nonce</td>
      <td>Input</td>
      <td>12-byte nonce (IV) used to construct the initial CTR counter value.</td>
      <td>UINT8</td>
      <td>ND</td>
    </tr>
    <tr>
      <td>output</td>
      <td>Output</td>
      <td>Ciphertext output of AES-128-CTR encryption, with its length rounded up to a multiple of 512 bytes.</td>
      <td>UINT8</td>
      <td>ND</td>
    </tr>
  </tbody></table>

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
