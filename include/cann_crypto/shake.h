/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_CRYPTO_SHAKE_H
#define CANN_CRYPTO_SHAKE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Device buffers contain contiguous UINT8 rows: input [batch, inputLength],
 * output [batch, outputLength]. Lengths are in bytes. Input may be NULL only
 * for empty messages (inputLength == 0). Buffers must not overlap.
 * The current ACL device and stream must match. NULL selects the default stream.
 * Returns an ACL error code; on success the stream has completed.
 */
int32_t CannCryptoShake128(
    const uint8_t* input, uint8_t* output, uint64_t batch, uint64_t inputLength, uint64_t outputLength, void* stream);
int32_t CannCryptoShake256(
    const uint8_t* input, uint8_t* output, uint64_t batch, uint64_t inputLength, uint64_t outputLength, void* stream);

#ifdef __cplusplus
}
#endif
#endif
