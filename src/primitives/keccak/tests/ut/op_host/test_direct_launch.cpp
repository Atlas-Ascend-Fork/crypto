/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "cann_crypto/keccak.h"
#include "cann_crypto/shake.h"
#include "acl/acl.h"
#include "../../../launch_utils.h"

#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

namespace {
int failures = 0;
int launches = 0;
int synchronizations = 0;
aclError deviceStatus = ACL_SUCCESS;
aclError queryStatus = ACL_SUCCESS;
aclError syncStatus = ACL_SUCCESS;
int64_t cores = 40;
uint32_t lastBlocks = 0;
uint32_t lastBatch = 0;
#ifdef CANN_CRYPTO_TEST_SHAKE
uint32_t lastBits = 0;
uint32_t lastInputLength = 0;
uint32_t lastOutputLength = 0;
uint32_t lastGroup = 0;
#endif
const void* lastInput = nullptr;
void* lastOutput = nullptr;
void* lastStream = nullptr;

void Check(bool value, int line)
{
    if (!value) {
        std::cerr << "check failed at line " << line << '\n';
        ++failures;
    }
}
#define CHECK(value) Check((value), __LINE__)

void CheckKeccak()
{
    std::vector<uint32_t> input(50U * 8192U);
    std::vector<uint32_t> output(input.size());
    auto* stream = reinterpret_cast<void*>(uintptr_t{0x1234});
    for (uint32_t batch : {1U, 7U, 8U, 9U, 64U, 255U, 256U, 257U, 8191U, 8192U}) {
        CHECK(CannCryptoKeccakF1600(input.data(), output.data(), batch, stream) == ACL_SUCCESS);
        CHECK(lastBatch == batch);
        CHECK(lastBlocks == (batch + 255U) / 256U);
        CHECK(lastInput == input.data() && lastOutput == output.data() && lastStream == stream);
    }
    const int before = launches;
    CHECK(CannCryptoKeccakF1600(nullptr, output.data(), 1U, stream) == ACL_ERROR_INVALID_PARAM);
    CHECK(CannCryptoKeccakF1600(input.data(), nullptr, 1U, stream) == ACL_ERROR_INVALID_PARAM);
    CHECK(CannCryptoKeccakF1600(input.data(), input.data() + 1, 1U, stream) == ACL_ERROR_INVALID_PARAM);
    CHECK(CannCryptoKeccakF1600(input.data(), output.data(), 0U, stream) == ACL_ERROR_INVALID_PARAM);
    CHECK(CannCryptoKeccakF1600(input.data(), output.data(), 8193U, stream) == ACL_ERROR_INVALID_PARAM);
    CHECK(CannCryptoKeccakF1600(input.data(), output.data(), UINT64_MAX, stream) == ACL_ERROR_INVALID_PARAM);
    CHECK(launches == before);
}

#ifdef CANN_CRYPTO_TEST_SHAKE
void CheckShake()
{
    uint8_t input[512]{};
    uint8_t output[1024]{};
    for (const auto fn : {CannCryptoShake128, CannCryptoShake256}) {
        const uint32_t bits = fn == CannCryptoShake128 ? 128U : 256U;
        CHECK(fn(input, output, 2U, 65U, 33U, nullptr) == ACL_SUCCESS);
        CHECK(lastBits == bits && lastBatch == 2U && lastInputLength == 65U && lastOutputLength == 33U);
        CHECK(lastInput == input && lastOutput == output && lastStream == nullptr);
        CHECK(lastBlocks == 1U && lastGroup == 64U);
        CHECK(fn(nullptr, output, 1U, 0U, 1U, nullptr) == ACL_SUCCESS);
        const int before = launches;
        CHECK(fn(nullptr, output, 1U, 1U, 1U, nullptr) == ACL_ERROR_INVALID_PARAM);
        CHECK(fn(input, nullptr, 1U, 1U, 1U, nullptr) == ACL_ERROR_INVALID_PARAM);
        CHECK(fn(input, input + 1, 2U, 65U, 33U, nullptr) == ACL_ERROR_INVALID_PARAM);
        CHECK(fn(input, output, 0U, 1U, 1U, nullptr) == ACL_ERROR_INVALID_PARAM);
        CHECK(fn(input, output, 1U, 1U, 0U, nullptr) == ACL_ERROR_INVALID_PARAM);
        CHECK(fn(input, output, UINT32_MAX, 0U, 1U, nullptr) == ACL_ERROR_INVALID_PARAM);
        CHECK(fn(input, output, 1U, UINT64_MAX, 1U, nullptr) == ACL_ERROR_INVALID_PARAM);
        CHECK(fn(input, output, 1U, 0U, UINT64_MAX, nullptr) == ACL_ERROR_INVALID_PARAM);
        CHECK(launches == before);
    }
}

#endif

void CheckRuntimeFailures()
{
    uint32_t input[50]{};
    uint32_t output[50]{};
    const int before = launches;
    deviceStatus = 101;
    CHECK(CannCryptoKeccakF1600(input, output, 1U, nullptr) == 101);
    deviceStatus = ACL_SUCCESS;
    queryStatus = 102;
    CHECK(CannCryptoKeccakF1600(input, output, 1U, nullptr) == 102);
#ifdef CANN_CRYPTO_TEST_SHAKE
    CHECK(CannCryptoShake128(nullptr, reinterpret_cast<uint8_t*>(output), 1U, 0U, 1U, nullptr) == 102);
#endif
    queryStatus = ACL_SUCCESS;
    for (int64_t count : {int64_t{0}, int64_t{-1}, int64_t{UINT32_MAX} + 1}) {
        cores = count;
        CHECK(CannCryptoKeccakF1600(input, output, 1U, nullptr) == ACL_ERROR_INVALID_PARAM);
    }
    cores = 40;
    CHECK(launches == before);
    syncStatus = 103;
    CHECK(CannCryptoKeccakF1600(input, output, 1U, nullptr) == 103);
#ifdef CANN_CRYPTO_TEST_SHAKE
    CHECK(CannCryptoShake256(nullptr, reinterpret_cast<uint8_t*>(output), 1U, 0U, 1U, nullptr) == 103);
#endif
    syncStatus = ACL_SUCCESS;
}
} // namespace

extern "C" aclError aclrtGetDevice(int32_t* device)
{
    *device = 2;
    return deviceStatus;
}

extern "C" aclError aclrtGetDeviceInfo(uint32_t device, aclrtDevAttr attr, int64_t* value)
{
    CHECK(device == 2U && attr == ACL_DEV_ATTR_VECTOR_CORE_NUM);
    *value = cores;
    return queryStatus;
}

extern "C" aclError aclrtSynchronizeStream(aclrtStream stream)
{
    CHECK(stream == lastStream);
    ++synchronizations;
    return syncStatus;
}

extern "C" void CannCryptoKeccakLaunch(
    uint32_t blockDim, void* stream, const uint32_t* input, uint32_t* output, uint32_t batch)
{
    ++launches;
    lastBlocks = blockDim;
    lastStream = stream;
    lastInput = input;
    lastOutput = output;
    lastBatch = batch;
}

#ifdef CANN_CRYPTO_TEST_SHAKE
extern "C" void CannCryptoShakeLaunch(
    uint32_t bits, uint32_t blockDim, void* stream, const uint8_t* input, uint8_t* output, uint32_t batch,
    uint32_t inputLength, uint32_t outputLength, uint32_t groupSize)
{
    ++launches;
    lastBits = bits;
    lastBlocks = blockDim;
    lastStream = stream;
    lastInput = input;
    lastOutput = output;
    lastBatch = batch;
    lastInputLength = inputLength;
    lastOutputLength = outputLength;
    lastGroup = groupSize;
}

#endif

int main()
{
    CheckKeccak();
#ifdef CANN_CRYPTO_TEST_SHAKE
    CheckShake();
#endif
    CheckRuntimeFailures();
    const auto* high = reinterpret_cast<const void*>(std::numeric_limits<uintptr_t>::max() - 1U);
    CHECK(!cann_crypto::detail::ValidBuffers(high, 4U, reinterpret_cast<void*>(uintptr_t{4096}), 4U));
    CHECK(!cann_crypto::detail::ValidBuffers(nullptr, 0U, high, 4U));
    CHECK(launches == synchronizations);
    return failures == 0 ? 0 : 1;
}
