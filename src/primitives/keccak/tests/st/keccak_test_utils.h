/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_CRYPTO_KECCAK_TEST_UTILS_H
#define CANN_CRYPTO_KECCAK_TEST_UTILS_H

#include <acl/acl.h>
#include "cann_crypto/keccak.h"
#include "../assets/sha3_ref.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace cann_crypto::test {

inline void RequireAcl(aclError status, const char* expression)
{
    if (status != ACL_SUCCESS) {
        const char* detail = aclGetRecentErrMsg();
        throw std::runtime_error(
            std::string("ACL 调用失败：") + expression + "，返回值=" + std::to_string(status) +
            (detail == nullptr ? "" : std::string("\n") + detail));
    }
}

class DeviceBuffer {
public:
    explicit DeviceBuffer(size_t logicalBytes) : logicalBytes_(logicalBytes)
    {
        const size_t allocationBytes = logicalBytes_ == 0 ? 1 : logicalBytes_;
        RequireAcl(aclrtMalloc(&data_, allocationBytes, ACL_MEM_MALLOC_HUGE_FIRST), "aclrtMalloc");
    }

    ~DeviceBuffer()
    {
        if (data_ != nullptr) {
            aclrtFree(data_);
        }
    }

    DeviceBuffer(const DeviceBuffer&) = delete;
    DeviceBuffer& operator=(const DeviceBuffer&) = delete;
    void* get() const { return data_; }
    size_t size() const { return logicalBytes_; }

private:
    void* data_ = nullptr;
    size_t logicalBytes_ = 0;
};

inline uint64_t Checksum(const std::vector<uint8_t>& bytes)
{
    uint64_t hash = 1469598103934665603ULL;
    for (uint8_t byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

inline void PackKeccakStates(const std::vector<uint8_t>& states, size_t batch, std::vector<uint32_t>& packed)
{
    packed.assign(50 * batch, 0U);
    for (size_t message = 0; message < batch; ++message) {
        const uint8_t* state = states.data() + message * 200;
        for (size_t lane = 0; lane < 25; ++lane) {
            const uint64_t value = CannCryptoRefLoad64Le(state + lane * 8);
            packed[(2 * lane) * batch + message] = static_cast<uint32_t>(value & 0xffffffffULL);
            packed[(2 * lane + 1) * batch + message] = static_cast<uint32_t>(value >> 32);
        }
    }
}

inline void UnpackKeccakStates(const std::vector<uint32_t>& packed, size_t batch, std::vector<uint8_t>& states)
{
    states.assign(batch * 200, 0U);
    for (size_t message = 0; message < batch; ++message) {
        uint8_t* state = states.data() + message * 200;
        for (size_t lane = 0; lane < 25; ++lane) {
            const uint64_t value = static_cast<uint64_t>(packed[(2 * lane) * batch + message]) |
                                   (static_cast<uint64_t>(packed[(2 * lane + 1) * batch + message]) << 32);
            CannCryptoRefStore64Le(state + lane * 8, value);
        }
    }
}

inline std::vector<uint8_t> KeccakReference(const std::vector<uint8_t>& inputStates, size_t batch)
{
    std::vector<uint8_t> expected = inputStates;
    for (size_t message = 0; message < batch; ++message) {
        uint64_t state[25];
        for (size_t lane = 0; lane < 25; ++lane) {
            state[lane] = CannCryptoRefLoad64Le(expected.data() + message * 200 + lane * 8);
        }
        CannCryptoRefKeccakF1600(state);
        for (size_t lane = 0; lane < 25; ++lane) {
            CannCryptoRefStore64Le(expected.data() + message * 200 + lane * 8, state[lane]);
        }
    }

    return expected;
}

inline void RunKeccakDevice(
    const std::vector<uint32_t>& inputPacked, std::vector<uint32_t>& outputPacked, size_t batch, aclrtStream stream)
{
    DeviceBuffer deviceInput(inputPacked.size() * sizeof(uint32_t));
    DeviceBuffer deviceOutput(outputPacked.size() * sizeof(uint32_t));
    RequireAcl(
        aclrtMemcpy(
            deviceInput.get(), deviceInput.size(), inputPacked.data(), inputPacked.size() * sizeof(uint32_t),
            ACL_MEMCPY_HOST_TO_DEVICE),
        "Keccak 输入由主机复制到设备");
    RequireAcl(aclrtMemset(deviceOutput.get(), deviceOutput.size(), 0, deviceOutput.size()), "Keccak 输出清零");

    RequireAcl(
        CannCryptoKeccakF1600(
            static_cast<const uint32_t*>(deviceInput.get()), static_cast<uint32_t*>(deviceOutput.get()), batch, stream),
        "Keccak kernel launch");
    RequireAcl(
        aclrtMemcpy(
            outputPacked.data(), outputPacked.size() * sizeof(uint32_t), deviceOutput.get(), deviceOutput.size(),
            ACL_MEMCPY_DEVICE_TO_HOST),
        "Keccak 输出由设备复制到主机");
}

inline bool CheckKeccakResult(const std::vector<uint8_t>& actual, const std::vector<uint8_t>& expected, size_t batch)
{
    const auto mismatch = std::mismatch(actual.begin(), actual.end(), expected.begin());
    if (mismatch.first != actual.end()) {
        const size_t offset = static_cast<size_t>(mismatch.first - actual.begin());
        std::cerr << "[失败] KeccakF1600Tensor"
                  << " batch=" << batch << " 消息序号=" << (offset / 200) << " 字节序号=" << (offset % 200)
                  << " 实际值=" << static_cast<unsigned>(*mismatch.first)
                  << " 期望值=" << static_cast<unsigned>(*mismatch.second) << std::endl;
        return false;
    }

    std::cout << "[通过] KeccakF1600Tensor"
              << " batch=" << batch << " 校验和=" << Checksum(actual) << std::endl;
    return true;
}

inline bool RunKeccakCase(size_t batch, aclrtStream stream)
{
    std::vector<uint8_t> inputStates(batch * 200);
    std::mt19937 generator(static_cast<uint32_t>(20260517U + batch));
    for (uint8_t& byte : inputStates) {
        byte = static_cast<uint8_t>(generator() & 0xffU);
    }

    const auto expected = KeccakReference(inputStates, batch);
    std::vector<uint32_t> inputPacked;
    PackKeccakStates(inputStates, batch, inputPacked);
    std::vector<uint32_t> outputPacked(50 * batch, 0U);
    RunKeccakDevice(inputPacked, outputPacked, batch, stream);
    std::vector<uint8_t> actual;
    UnpackKeccakStates(outputPacked, batch, actual);
    return CheckKeccakResult(actual, expected, batch);
}

} // namespace cann_crypto::test

#endif
