/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "cann_crypto/aes.h"
#include "../../../../op_host/aes_launch.h"
#include "../../../../op_kernel/aes_tiling.h"
#include "acl/acl.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
constexpr aclError FAILURE = 507035;
std::vector<void*> allocations;
std::vector<int> launches;
int syncCount = 0;
int failSync = 0;
int failMalloc = 0;
int mallocCount = 0;
bool failCopy = false;
bool failSet = false;
bool paddedCorrectly = false;
uint8_t* pendingInput = nullptr;
uint32_t inputLength = 0;
Aes128GcmTilingData gcmTiling{};
void* expectedStream = reinterpret_cast<void*>(42);
void Check(bool condition)
{
    if (!condition) {
        throw std::runtime_error("AES Host contract check failed");
    }
}
void Reset()
{
    Check(allocations.empty());
    launches.clear();
    syncCount = failSync = failMalloc = mallocCount = 0;
    failCopy = failSet = paddedCorrectly = false;
    pendingInput = nullptr;
}
} // namespace

aclError aclrtMalloc(void** pointer, size_t bytes, int)
{
    if (++mallocCount == failMalloc) {
        return FAILURE;
    }
    *pointer = std::malloc(bytes);
    Check(*pointer != nullptr);
    allocations.push_back(*pointer);
    return ACL_SUCCESS;
}
aclError aclrtFree(void* pointer)
{
    const auto found = std::find(allocations.begin(), allocations.end(), pointer);
    Check(found != allocations.end());
    allocations.erase(found);
    std::free(pointer);
    return ACL_SUCCESS;
}
aclError aclrtMemcpy(void* destination, size_t capacity, const void* source, size_t bytes, int)
{
    if (failCopy) {
        return FAILURE;
    }
    Check(bytes <= capacity && pendingInput == nullptr);
    std::copy_n(static_cast<const uint8_t*>(source), bytes, static_cast<uint8_t*>(destination));
    return ACL_SUCCESS;
}
aclError aclrtMemset(void* destination, size_t capacity, int value, size_t bytes)
{
    if (failSet) {
        return FAILURE;
    }
    Check(bytes <= capacity);
    std::fill_n(static_cast<uint8_t*>(destination), bytes, static_cast<uint8_t>(value));
    return ACL_SUCCESS;
}
aclError aclrtSynchronizeStream(aclrtStream stream)
{
    Check(stream == expectedStream);
    if (++syncCount == failSync) {
        return FAILURE;
    }
    if (pendingInput != nullptr) {
        std::fill(pendingInput, pendingInput + 17, 7);
        pendingInput = nullptr;
    }
    return ACL_SUCCESS;
}
extern "C" void CannCryptoLaunchAesCtr(void* input, void*, void* tiling, void* stream)
{
    Check(stream == expectedStream);
    launches.push_back(0);
    const auto& metadata = *static_cast<ascend_ops::AesCtr::Aes128CtrTilingData*>(tiling);
    inputLength = metadata.input_length;
    const auto* bytes = static_cast<uint8_t*>(input);
    paddedCorrectly = bytes[0] == 7 && bytes[16] == 7 && bytes[17] == 0 && bytes[511] == 0;
}
extern "C" void CannCryptoLaunchAesGcmPrecompute(void*, void* tiling, void* stream)
{
    Check(stream == expectedStream);
    launches.push_back(1);
    gcmTiling = *static_cast<Aes128GcmTilingData*>(tiling);
}
extern "C" void CannCryptoLaunchAesGcmEncrypt(uint32_t size, void*, void*, void*, void* stream)
{
    Check(stream == expectedStream && size == gcmTiling.totalDataSize);
    launches.push_back(2);
}
extern "C" void CannCryptoLaunchAesGcmGhash(void*, void*, void*, void*, void* stream)
{
    Check(stream == expectedStream);
    launches.push_back(3);
}
extern "C" void CannCryptoLaunchAesGcmTag(void*, void*, void*, void*, void* stream)
{
    Check(stream == expectedStream);
    launches.push_back(4);
}

namespace {
struct TestData {
    std::array<uint8_t, 2560> input{};
    std::array<uint8_t, 2576> output{};
    std::array<uint8_t, 176> key{};
    std::array<uint8_t, 12> nonce{};
    std::array<uint8_t, 32> aad{};

    TestData()
    {
        input.fill(7);
        key.fill(11);
        nonce.fill(13);
        aad.fill(17);
    }
    aclError Ctr()
    {
        return CannCryptoAes128Ctr(input.data(), 17, output.data(), 512, key.data(), nonce.data(), expectedStream);
    }
    aclError Gcm()
    {
        return CannCryptoAes128Gcm(
            input.data(), input.size(), output.data(), output.size(), key.data(), nonce.data(), aad.data(), aad.size(),
            expectedStream);
    }
};

void TestCtrInputAndStatus(TestData& data)
{
    Reset();
    data.input.fill(99);
    pendingInput = data.input.data(); // Simulate an unfinished producer on the supplied stream.
    Check(data.Ctr() == ACL_SUCCESS);
    Check(paddedCorrectly && inputLength == 17 && syncCount == 2 && allocations.empty());
    Reset();
    failSync = 1;
    Check(data.Ctr() == FAILURE && launches.empty() && allocations.empty());
    Reset();
    failSync = 2;
    Check(data.Ctr() == FAILURE && launches == std::vector<int>({0}) && allocations.empty());
}

void TestGcmStagesAndMetadata(TestData& data)
{
    for (int stage = 1; stage <= 4; ++stage) {
        Reset();
        failSync = stage;
        Check(data.Gcm() == FAILURE && syncCount == stage && launches.size() == static_cast<size_t>(stage));
        Check(allocations.empty());
    }
    Reset();
    Check(data.Gcm() == ACL_SUCCESS && launches == std::vector<int>({1, 2, 3, 4}));
    Check(gcmTiling.totalDataSize == 2560 && gcmTiling.numCores == 40 && gcmTiling.ghashCoreNum == 20);
    Check(std::memcmp(gcmTiling.rd_key, data.key.data(), data.key.size()) == 0);
    Check(std::memcmp(gcmTiling.nonce, data.nonce.data(), data.nonce.size()) == 0);
    Check(gcmTiling.aad_len == 32 && std::memcmp(gcmTiling.aad, data.aad.data(), data.aad.size()) == 0);
}

void TestAllocationAndCopyFailures(TestData& data)
{
    for (int allocation = 1; allocation <= 3; ++allocation) {
        Reset();
        failMalloc = allocation;
        Check(data.Gcm() == FAILURE && launches.empty() && allocations.empty());
    }
    Reset();
    failCopy = true;
    Check(data.Gcm() == FAILURE && launches.empty() && allocations.empty());
    Reset();
    failSet = true;
    Check(data.Ctr() == FAILURE && launches.empty() && allocations.empty());
}

void TestEmptyAndInvalidInputs(TestData& data)
{
    Reset();
    Check(
        CannCryptoAes128Gcm(
            nullptr, 0, data.output.data(), 16, data.key.data(), data.nonce.data(), nullptr, 0, expectedStream) ==
        ACL_SUCCESS);
    Check(gcmTiling.totalDataSize == 0 && gcmTiling.aad_len == 0);
    Reset();
    Check(
        CannCryptoAes128Ctr(nullptr, 0, data.output.data(), 512, data.key.data(), data.nonce.data(), expectedStream) ==
        ACL_ERROR_INVALID_PARAM);
    Check(
        CannCryptoAes128Ctr(
            data.input.data(), (1ULL << 32) - 511, data.output.data(), (1ULL << 32), data.key.data(), data.nonce.data(),
            expectedStream) == ACL_ERROR_INVALID_PARAM);
    Check(
        CannCryptoAes128Ctr(
            data.input.data(), 17, data.output.data(), 511, data.key.data(), data.nonce.data(), expectedStream) ==
        ACL_ERROR_INVALID_PARAM);
    Check(
        CannCryptoAes128Gcm(
            data.input.data(), 2560, data.output.data(), 2575, data.key.data(), data.nonce.data(), data.aad.data(), 32,
            expectedStream) == ACL_ERROR_INVALID_PARAM);
    Check(
        CannCryptoAes128Gcm(
            nullptr, 0, data.output.data(), 16, data.key.data(), data.nonce.data(), nullptr, 1, expectedStream) ==
        ACL_ERROR_INVALID_PARAM);
    Check(
        CannCryptoAes128Gcm(
            nullptr, 0, data.output.data(), 16, data.key.data(), data.nonce.data(), data.aad.data(), 33,
            expectedStream) == ACL_ERROR_INVALID_PARAM);
    Check(launches.empty() && allocations.empty());
}
} // namespace

int main()
{
    TestData data;
    TestCtrInputAndStatus(data);
    TestGcmStagesAndMetadata(data);
    TestAllocationAndCopyFailures(data);
    TestEmptyAndInvalidInputs(data);
    std::cout << "AES_HOST_RESULT=PASS\n";
}
