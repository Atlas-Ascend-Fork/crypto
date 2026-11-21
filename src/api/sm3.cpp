/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#include "cann_crypto/sm3.h"
#include "../algorithms/hash/sm3/sm3_internal.h"
#include <new>

namespace {
struct Range {
    uintptr_t begin;
    uintptr_t end;
};
bool MakeRange(const void* p, uint64_t n, Range& r)
{
    const auto a = reinterpret_cast<uintptr_t>(p);
    if ((!p && n) || n > UINTPTR_MAX - a)
        return false;
    r = {a, a + static_cast<uintptr_t>(n)};
    return true;
}
bool Overlap(Range a, Range b) { return a.begin != a.end && b.begin != b.end && a.begin < b.end && b.begin < a.end; }
} // namespace
extern "C" int32_t CannCryptoSm3(const uint8_t* input, uint8_t* output, uint64_t batch, uint64_t length, void* stream)
{
    if (length > UINT32_MAX || batch > UINT32_MAX / 32)
        return CANN_CRYPTO_SM3_INPUT_TOO_LONG;
    if (!batch)
        return CANN_CRYPTO_SM3_SUCCESS;
    const uint64_t inputBytes = batch * length;
    if (inputBytes > UINT32_MAX)
        return CANN_CRYPTO_SM3_INPUT_TOO_LONG;
    Range in{}, out{};
    if (!stream || !MakeRange(input, inputBytes, in) || !MakeRange(output, batch * 32, out) || Overlap(in, out) ||
        reinterpret_cast<uintptr_t>(output) % 32)
        return CANN_CRYPTO_SM3_BAD_ARGUMENT;
    try {
        return CannCryptoSm3RunDirectNpu(
            input, output, static_cast<uint32_t>(batch), static_cast<uint32_t>(length), stream);
    } catch (const std::bad_alloc&) {
        return CANN_CRYPTO_SM3_ALLOCATION_FAILURE;
    } catch (...) {
        return CANN_CRYPTO_SM3_RUNTIME_FAILURE;
    }
}
