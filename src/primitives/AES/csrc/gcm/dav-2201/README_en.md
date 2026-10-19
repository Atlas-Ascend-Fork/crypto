# AES-128-GCM

English | [简体中文](README.md)

## Supported Products

| Product | Supported |
| ---- | :----:|
|Atlas A2 training series / Atlas A2 inference series|√|

## Function

- Operator function: perform AES-128-GCM authenticated encryption.

- Formula:

$$
\text{output} = \text{AES-128-GCM}(\text{input}, \text{key}, \text{nonce}, \text{aad})
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
      <td>Plaintext input for AES-128-GCM encryption.</td>
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
      <td>12-byte nonce (IV) used to construct the initial GCM counter value.</td>
      <td>UINT8</td>
      <td>ND</td>
    </tr>
    <tr>
      <td>aad</td>
      <td>Input</td>
      <td>Additional authenticated data (AAD), up to 32 bytes.</td>
      <td>UINT8</td>
      <td>ND</td>
    </tr>
    <tr>
      <td>output</td>
      <td>Output</td>
      <td>AES-128-GCM ciphertext followed by a 16-byte authentication tag. The ciphertext length is rounded up to a multiple of 512 bytes; the total length is the rounded length + 16.</td>
      <td>UINT8</td>
      <td>ND</td>
    </tr>
  </tbody></table>

## Constraints

1. A nonempty `input` must have a length that is a multiple of 2560 bytes and no more than 4,294,965,760 bytes so that the length, rounded up to a multiple of 512 bytes, fits in the 32-bit device metadata. The 2560-byte restriction matches the [invocation examples](../../../examples/README_en.md): the current GHASH implementation distributes 16-byte blocks across 20 cores and uses the same partition length during merging; the block count after padding to a multiple of 512 bytes must be divisible by 20. This is a caller-side constraint. The Host entry point currently checks only the upper bound, so accepting another length does not imply support for its authentication tag. The upper bound above combines the Host check of 4,294,966,784 bytes with the 2560-byte chunking constraint; it does not indicate that this length has been validated on a device.
2. The `key` input must contain at least 176 bytes, including all 11 rounds of expanded AES round keys.
3. The `nonce` input must contain at least 12 bytes.
4. The `aad` input must contain no more than 32 bytes.
5. The total `output` length is the padded ciphertext length plus the 16-byte authentication tag.

## Contributions

| Contributor | Organization | Operator | Date | Contribution |
| ---- | ---- | ---- | ---- | ---- |
| 汤佳琪 | Prof. Hongzhi Zhao's team, Beijing Jiaotong University | AES-128-GCM | 2026/06/16 | Adapted the AES-128-GCM operator for the open-source repository |
| 范志鹏 | Prof. Hongzhi Zhao's team, Beijing Jiaotong University | AES-128-GCM | 2026/06/16 | Adapted the AES-128-GCM operator for the open-source repository |
| 韩智惟 | Prof. Hongzhi Zhao's team, Beijing Jiaotong University | AES-128-GCM | 2026/06/16 | Adapted the AES-128-GCM operator for the open-source repository |
|  张昕  | Prof. Hongzhi Zhao's team, Beijing Jiaotong University | AES-128-GCM | 2026/06/16 | Adapted the AES-128-GCM operator for the open-source repository |
