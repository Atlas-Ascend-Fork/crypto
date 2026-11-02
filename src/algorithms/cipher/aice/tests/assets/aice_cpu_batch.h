/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_CRYPTO_TEST_AICE_CPU_H
#define CANN_CRYPTO_TEST_AICE_CPU_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AICE_CPU_ENCRYPT = 0,
    AICE_CPU_DECRYPT = 1,
} aice_cpu_batch_operation_t;

typedef enum {
    AICE_CPU_SUCCESS = 0,
    AICE_CPU_INVALID_ARGUMENT = 1,
    AICE_CPU_ALLOCATION_FAILURE = 2,
} aice_cpu_batch_status_t;

/* IV is 12 bytes; each lane appends its global index as a little-endian uint32_t. */
#define DECLARE(B) \
    int AiceCpuCrypt##B(uint32_t, int, const uint8_t*, const uint8_t*, const uint8_t*, uint8_t*, uint32_t)
DECLARE(128);
DECLARE(256);
DECLARE(512);
#undef DECLARE

static inline aice_cpu_batch_status_t aice_cpu_batch_crypt(
    uint32_t batch, uint32_t dim, aice_cpu_batch_operation_t operation, const uint8_t key[32], const uint8_t iv[12],
    const uint8_t* input, uint8_t* output, uint32_t length)
{
    if ((batch != 128 && batch != 256 && batch != 512) || dim == 0 || (uint64_t)batch * dim > UINT32_MAX ||
        key == NULL || iv == NULL || (operation != AICE_CPU_ENCRYPT && operation != AICE_CPU_DECRYPT) ||
        (length != 0 && (input == NULL || output == NULL)))
        return AICE_CPU_INVALID_ARGUMENT;
    if (length == 0)
        return AICE_CPU_SUCCESS;
    const int decrypt = operation == AICE_CPU_DECRYPT;
    switch (batch) {
        case 128:
            AiceCpuCrypt128(dim, decrypt, key, iv, input, output, length);
            break;
        case 256:
            AiceCpuCrypt256(dim, decrypt, key, iv, input, output, length);
            break;
        case 512:
            AiceCpuCrypt512(dim, decrypt, key, iv, input, output, length);
            break;
    }
    return AICE_CPU_SUCCESS;
}

#ifdef __cplusplus
}
#endif

#endif
