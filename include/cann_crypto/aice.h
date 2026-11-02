/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef CANN_CRYPTO_AICE_H
#define CANN_CRYPTO_AICE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Synchronous AICE on the current ACL device; returns an ACL error code.
 * Data pointers refer to device memory: key is 32 bytes, IV is 12 bytes.
 * inputBytes <= UINT32_MAX; outputCapacity >= inputBytes. Exact in-place
 * operation is supported; other overlaps with output are rejected.
 * batchSize: 128/256/512 lanes; dim: device-supported block count > 0;
 * batchSize * dim <= UINT32_MAX. Decryption uses the same key, IV, batchSize,
 * dim and length. Use a distinct IV for each encryption under a key.
 * stream == NULL selects the default stream. Zero-length calls accept NULL
 * data pointers and still synchronize the stream.
 */
int32_t CannCryptoAiceEncrypt(
    const uint8_t* input, uint64_t inputBytes, uint8_t* output, uint64_t outputCapacity, const uint8_t* key,
    const uint8_t* iv, uint32_t batchSize, uint32_t dim, void* stream);
int32_t CannCryptoAiceDecrypt(
    const uint8_t* input, uint64_t inputBytes, uint8_t* output, uint64_t outputCapacity, const uint8_t* key,
    const uint8_t* iv, uint32_t batchSize, uint32_t dim, void* stream);

#ifdef __cplusplus
}
#endif
#endif
