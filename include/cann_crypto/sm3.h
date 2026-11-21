/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef CANN_CRYPTO_SM3_H
#define CANN_CRYPTO_SM3_H
#include <stdint.h>
#if defined(_WIN32)
#if defined(CANN_CRYPTO_SM3_BUILDING)
#define CANN_CRYPTO_SM3_API __declspec(dllexport)
#else
#define CANN_CRYPTO_SM3_API __declspec(dllimport)
#endif
#else
#define CANN_CRYPTO_SM3_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define CANN_CRYPTO_SM3_SUCCESS 0
#define CANN_CRYPTO_SM3_BAD_ARGUMENT 1
#define CANN_CRYPTO_SM3_INPUT_TOO_LONG 3
#define CANN_CRYPTO_SM3_ALLOCATION_FAILURE 5
#define CANN_CRYPTO_SM3_RUNTIME_FAILURE 6

/* Native Kernel direct-call entry: input/output are DEVICE buffers, holding
 * batch consecutive equal-length messages and batch consecutive 32-byte hashes.
 * No allocation, packing or H2D/D2H is performed. Output is 32-byte aligned;
 * nonempty input/output ranges are disjoint. Caller owns ACL/context/stream.
 * Synchronous, respecting work already queued on the supplied stream.
 * Validation errors do not write output; runtime failures leave device output
 * unspecified. Empty batch accepts NULL buffers/stream. Dimensions and total
 * input/output bytes fit UINT32_MAX.
 */
CANN_CRYPTO_SM3_API int32_t
CannCryptoSm3(const uint8_t* input, uint8_t* output, uint64_t batch, uint64_t inputLength, void* stream);
#ifdef __cplusplus
}
#endif
#endif
