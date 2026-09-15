/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_CRYPTO_SHA3_REF_H
#define CANN_CRYPTO_SHA3_REF_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
/* Keccak 与 SHAKE 的独立测试参考实现。 */
uint64_t CannCryptoRefLoad64Le(const uint8_t *input);
void CannCryptoRefStore64Le(uint8_t *output, uint64_t value);
void CannCryptoRefKeccakF1600(uint64_t state[25]);
int CannCryptoRefShake(uint32_t algorithm, const uint8_t *input, size_t inputLength,
    uint8_t *output, size_t outputLength);
#ifdef __cplusplus
}
#endif
#endif
