/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef AICE_API_CPU_H
#define AICE_API_CPU_H

#include <stddef.h>
#include <stdint.h>

/* AICEv5 algorithm parameters (see specs/AICEv5_algorithm.md) */
#define STATE 37 /* number of 16-bit state words */
#ifndef BATCH
#define BATCH 32 /* number of parallel keystream lanes */
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Parallel-lane profile (not the full 128-bit nonce reference API).
   key: 32 bytes; iv: 12 effective bytes, loaded little-endian into S0..S5.
   S6=uint16_t(lane_id), S7=uint16_t(lane_id >> 16); an optional 16-byte legacy IV buffer has its
   final four bytes ignored. They MUST NOT be used to distinguish messages.
   One-shot API: each call reinitializes; tails complete a 37-step cycle.
   Odd final byte is zero-extended to a 16-bit word and encrypted/decrypted;
   exactly msg_len bytes are returned. Caller buffers may alias in-place.
   Lane IDs are uint32_t (0..4294967295); never reuse or wrap a lane ID.
 */
void aice_encrypt_cpu(
    const uint8_t* key, const uint8_t* iv, const uint8_t* plaintext, uint8_t* ciphertext, size_t msg_len);

void aice_decrypt_cpu(
    const uint8_t* key, const uint8_t* iv, const uint8_t* ciphertext, uint8_t* plaintext, size_t msg_len);

#ifdef __cplusplus
}
#endif

#endif /* AICE_API_CPU_H */
