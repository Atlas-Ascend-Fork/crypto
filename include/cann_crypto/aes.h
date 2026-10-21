/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_CRYPTO_AES_H
#define CANN_CRYPTO_AES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Synchronous AES-128 encryption on the current ACL device and stream.
 * input/output are contiguous device byte buffers and must not overlap.
 * roundKeys points to 176 Host bytes (11 expanded AES-128 round keys); nonce
 * points to 12 Host bytes. NULL stream selects the default stream.
 * inputBytes must be <= 4,294,966,784. CTR additionally requires inputBytes > 0.
 * outputCapacity must be >= ceil(inputBytes / 512) * 512 for CTR, plus 16 for GCM.
 * The input is zero-padded internally; only inputBytes output bytes are valid
 * CTR ciphertext. GCM authenticates the padded plaintext and appends its tag
 * at the padded ciphertext length. Its currently supported nonempty lengths
 * are multiples of 2560 bytes, as required by the existing GHASH partitioning.
 * aad points to up to 32 Host bytes, and may be NULL when aadBytes is zero.
 * input may be NULL for empty GCM plaintext. On success the stream is complete.
 * Returns an ACL error code, including allocation/copy/synchronization errors.
 */
int32_t CannCryptoAes128Ctr(
    const uint8_t* input, uint64_t inputBytes, uint8_t* output, uint64_t outputCapacity, const uint8_t* roundKeys,
    const uint8_t* nonce, void* stream);
int32_t CannCryptoAes128Gcm(
    const uint8_t* input, uint64_t inputBytes, uint8_t* output, uint64_t outputCapacity, const uint8_t* roundKeys,
    const uint8_t* nonce, const uint8_t* aad, uint32_t aadBytes, void* stream);

#ifdef __cplusplus
}
#endif
#endif
