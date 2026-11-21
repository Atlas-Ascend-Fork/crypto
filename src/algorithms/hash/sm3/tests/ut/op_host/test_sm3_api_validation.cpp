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
#include "sm3_internal.h"
#include <new>
#include <stdexcept>

namespace {
struct Invocation {
    const uint8_t* input;
    uint8_t* output;
    uint32_t count;
    uint32_t length;
    void* stream;
};

Invocation last{};
unsigned calls = 0;
enum class Result { Success, Failure, AllocationError };
Result nextResult = Result::Success;

void Check(bool condition, const char* description)
{
    if (!condition)
        throw std::runtime_error(description);
}

void CheckRejectedInputs(uint8_t* input, uint8_t* output, void* stream)
{
    Check(CannCryptoSm3(nullptr, nullptr, 0, 0, nullptr) == CANN_CRYPTO_SM3_SUCCESS, "empty batch");
    Check(CannCryptoSm3(input, output, 1, 1, nullptr) == CANN_CRYPTO_SM3_BAD_ARGUMENT, "missing stream");
    Check(CannCryptoSm3(nullptr, output, 1, 1, stream) == CANN_CRYPTO_SM3_BAD_ARGUMENT, "missing input");
    Check(CannCryptoSm3(input, nullptr, 1, 1, stream) == CANN_CRYPTO_SM3_BAD_ARGUMENT, "missing output");
    Check(CannCryptoSm3(input, output + 1, 1, 1, stream) == CANN_CRYPTO_SM3_BAD_ARGUMENT, "output alignment");
    Check(CannCryptoSm3(input, input + 32, 1, 33, stream) == CANN_CRYPTO_SM3_BAD_ARGUMENT, "partial overlap");
    Check(
        CannCryptoSm3(input, output, 1, uint64_t(UINT32_MAX) + 1, stream) == CANN_CRYPTO_SM3_INPUT_TOO_LONG,
        "message length limit");
    Check(CannCryptoSm3(input, output, 2, UINT32_MAX, stream) == CANN_CRYPTO_SM3_INPUT_TOO_LONG, "input size limit");
    Check(
        CannCryptoSm3(input, output, UINT32_MAX / 32 + 1, 0, stream) == CANN_CRYPTO_SM3_INPUT_TOO_LONG,
        "output size limit");
    Check(calls == 0, "no device launch for invalid input or empty batch");
}

void CheckForwarding(uint8_t* input, uint8_t* output, void* stream)
{
    Check(CannCryptoSm3(nullptr, output, 1, 0, stream) == CANN_CRYPTO_SM3_SUCCESS, "empty message");
    Check(calls == 1 && last.input == nullptr && last.length == 0, "empty message reaches the device");
    Check(CannCryptoSm3(input, output, 2, 32, stream) == CANN_CRYPTO_SM3_SUCCESS, "valid batch");
    Check(
        calls == 2 && last.input == input && last.output == output && last.count == 2 && last.length == 32 &&
            last.stream == stream,
        "forward the original buffers, count, length and stream");

    nextResult = Result::Failure;
    Check(CannCryptoSm3(input, output, 1, 32, stream) == CANN_CRYPTO_SM3_RUNTIME_FAILURE, "device failure");
    nextResult = Result::AllocationError;
    Check(CannCryptoSm3(input, output, 1, 32, stream) == CANN_CRYPTO_SM3_ALLOCATION_FAILURE, "allocation failure");
}
} // namespace

int32_t CannCryptoSm3RunDirectNpu(const uint8_t* input, uint8_t* output, uint32_t count, uint32_t length, void* stream)
{
    ++calls;
    last = {input, output, count, length, stream};
    if (nextResult == Result::AllocationError)
        throw std::bad_alloc();
    if (nextResult == Result::Failure)
        return CANN_CRYPTO_SM3_RUNTIME_FAILURE;
    return CANN_CRYPTO_SM3_SUCCESS;
}

int main()
{
    alignas(32) uint8_t input[64]{};
    alignas(32) uint8_t output[64]{};
    int stream = 0;
    CheckRejectedInputs(input, output, &stream);
    CheckForwarding(input, output, &stream);
}
