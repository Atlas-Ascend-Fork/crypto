# AES-128-GCM

## 产品支持情况

| 产品 | 是否支持 |
| ---- | :----:|
|Atlas A2 训练系列产品 / Atlas A2 推理系列产品|√|

## 功能说明

- 算子功能：完成 AES-128-GCM 认证加密计算。

- 计算公式：

$$
\text{output} = \text{AES-128-GCM}(\text{input}, \text{key}, \text{nonce}, \text{aad})
$$

## 参数说明

<table style="table-layout: fixed; width: 980px"><colgroup>
  <col style="width: 100px">
  <col style="width: 150px">
  <col style="width: 280px">
  <col style="width: 330px">
  <col style="width: 120px">
  </colgroup>
  <thead>
    <tr>
      <th>参数名</th>
      <th>输入/输出/属性</th>
      <th>描述</th>
      <th>数据类型</th>
      <th>数据格式</th>
    </tr></thead>
  <tbody>
    <tr>
      <td>input</td>
      <td>输入</td>
      <td>待进行 AES-128-GCM 加密计算的明文入参。</td>
      <td>UINT8</td>
      <td>ND</td>
    </tr>
    <tr>
      <td>key</td>
      <td>输入</td>
      <td>预扩展的 AES-128 轮密钥，共 11 轮 176 字节（44 个 uint32_t）。</td>
      <td>UINT8</td>
      <td>ND</td>
    </tr>
    <tr>
      <td>nonce</td>
      <td>输入</td>
      <td>12 字节 nonce（IV），用于构造 GCM 计数器初始值。</td>
      <td>UINT8</td>
      <td>ND</td>
    </tr>
    <tr>
      <td>add</td>
      <td>输入</td>
      <td>附加认证数据（Additional Authenticated Data），长度不超过 32 字节。</td>
      <td>UINT8</td>
      <td>ND</td>
    </tr>
    <tr>
      <td>output</td>
      <td>输出</td>
      <td>AES-128-GCM 加密后的密文与 16 字节认证标签，密文长度按 512 字节向上对齐，总长度为对齐后长度 + 16。</td>
      <td>UINT8</td>
      <td>ND</td>
    </tr>
  </tbody></table>

## 约束说明

1. 输入 `input` 长度必须为 20KB的倍数。
2. 输入 `key` 长度必须至少为 176 字节，包含完整的 11 轮 AES 扩展轮密钥。
3. 输入 `nonce` 长度必须至少为 12 字节。
4. 输入 `aad` 长度必须不超过 32 字节。
5. 输出 `output` 总长度为对齐后的密文长度加上 16 字节认证标签。

## 贡献说明

| 贡献者 | 贡献方 | 贡献算子 | 贡献时间 | 贡献内容 |
| ---- | ---- | ---- | ---- | ---- |
| 汤佳琪 | 北京交通大学-赵宏智老师团队 | AES-128-GCM | 2026/06/16 | AES-128-GCM算子适配开源仓 |
| 范志鹏 | 北京交通大学-赵宏智老师团队 | AES-128-GCM | 2026/06/16 | AES-128-GCM算子适配开源仓 |
| 韩智惟 | 北京交通大学-赵宏智老师团队 | AES-128-GCM | 2026/06/16 | AES-128-GCM算子适配开源仓 |
|  张昕  | 北京交通大学-赵宏智老师团队 | AES-128-GCM | 2026/06/16 | AES-128-GCM算子适配开源仓 |
