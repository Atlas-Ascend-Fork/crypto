/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_CRYPTO_KECCAK_H
#define CANN_CRYPTO_KECCAK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Device buffers contain contiguous UINT32 states in [50, batch] layout.
 * batch must be in [1, 8192]. Input and output must not overlap.
 * The current ACL device and stream must match. NULL selects the default stream.
 * Returns an ACL error code; on success the stream has completed.
 */
int32_t CannCryptoKeccakF1600(const uint32_t* input, uint32_t* output, uint64_t batch, void* stream);

#ifdef __cplusplus
}
#endif
#endif
