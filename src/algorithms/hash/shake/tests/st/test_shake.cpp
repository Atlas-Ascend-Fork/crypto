/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <acl/acl.h>
#include "cann_crypto/shake.h"
#include <openssl/evp.h>

#include "../../../../../primitives/keccak/tests/st/keccak_test_utils.h"
#include "../../../../../primitives/keccak/examples/acl_example.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using cann_crypto::test::Checksum;
using cann_crypto::test::DeviceBuffer;
using cann_crypto::test::RequireAcl;
using cann_crypto::test::RunKeccakCase;
using LaunchFn = int32_t (*)(const uint8_t*, uint8_t*, uint64_t, uint64_t, uint64_t, void*);
using EvpFn = const EVP_MD* (*)();

struct Algorithm {
    const char* name;
    size_t rate;
    size_t fastOutputLength;
    LaunchFn launch;
    EvpFn evp;
};

struct TestCase {
    size_t batch;
    size_t inputLength;
    size_t outputLength;
    uint32_t seed;
};

const Algorithm SHAKE128 = {"SHAKE128", 168, 32, CannCryptoShake128, EVP_shake128};
const Algorithm SHAKE256 = {"SHAKE256", 136, 64, CannCryptoShake256, EVP_shake256};

void OpenSslShake(
    const Algorithm& algorithm, const uint8_t* input, size_t inputLength, uint8_t* output, size_t outputLength)
{
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    if (context == nullptr) {
        throw std::runtime_error("EVP_MD_CTX_new 创建失败");
    }

    const uint8_t empty = 0;
    const uint8_t* inputPointer = inputLength == 0 ? &empty : input;
    const int initStatus = EVP_DigestInit_ex(context, algorithm.evp(), nullptr);
    const int updateStatus = EVP_DigestUpdate(context, inputPointer, inputLength);
    const int finalStatus = EVP_DigestFinalXOF(context, output, outputLength);
    EVP_MD_CTX_free(context);
    if (initStatus != 1 || updateStatus != 1 || finalStatus != 1) {
        throw std::runtime_error("OpenSSL SHAKE 参考计算失败");
    }
}

std::vector<uint8_t> MakeInput(const TestCase& testCase)
{
    std::vector<uint8_t> input(testCase.batch * testCase.inputLength);
    std::mt19937 generator(testCase.seed);
    for (uint8_t& byte : input) {
        byte = static_cast<uint8_t>(generator() & 0xffU);
    }

    if (testCase.batch > 0 && testCase.inputLength > 0) {
        std::fill(input.begin(), input.begin() + static_cast<std::ptrdiff_t>(testCase.inputLength), 0U);
    }
    if (testCase.batch > 1 && testCase.inputLength > 0) {
        for (size_t byte = 0; byte < testCase.inputLength; ++byte) {
            input[testCase.inputLength + byte] = static_cast<uint8_t>(byte & 0xffU);
        }
    }
    return input;
}

std::vector<uint8_t> ShakeReference(
    const Algorithm& algorithm, const TestCase& testCase, const std::vector<uint8_t>& input)
{
    std::vector<uint8_t> expected(testCase.batch * testCase.outputLength, 0U);

    for (size_t message = 0; message < testCase.batch; ++message) {
        const uint8_t* inputRow = testCase.inputLength == 0 ? nullptr : input.data() + message * testCase.inputLength;
        OpenSslShake(
            algorithm, inputRow, testCase.inputLength, expected.data() + message * testCase.outputLength,
            testCase.outputLength);
    }

    return expected;
}

void RunShakeDevice(
    const Algorithm& algorithm, const TestCase& testCase, const std::vector<uint8_t>& input,
    std::vector<uint8_t>& actual, aclrtStream stream)
{
    DeviceBuffer deviceInput(input.size());
    DeviceBuffer deviceOutput(actual.size());
    if (!input.empty()) {
        RequireAcl(
            aclrtMemcpy(deviceInput.get(), deviceInput.size(), input.data(), input.size(), ACL_MEMCPY_HOST_TO_DEVICE),
            "input H2D");
    }
    RequireAcl(aclrtMemset(deviceOutput.get(), deviceOutput.size(), 0, deviceOutput.size()), "output memset");

    RequireAcl(
        algorithm.launch(
            static_cast<const uint8_t*>(deviceInput.get()), static_cast<uint8_t*>(deviceOutput.get()), testCase.batch,
            testCase.inputLength, testCase.outputLength, stream),
        "SHAKE kernel launch");
    RequireAcl(
        aclrtMemcpy(actual.data(), actual.size(), deviceOutput.get(), deviceOutput.size(), ACL_MEMCPY_DEVICE_TO_HOST),
        "输出由设备复制到主机");
}

bool CheckShakeResult(
    const Algorithm& algorithm, const TestCase& testCase, const std::vector<uint8_t>& actual,
    const std::vector<uint8_t>& expected)
{
    const auto mismatch = std::mismatch(actual.begin(), actual.end(), expected.begin());
    if (mismatch.first != actual.end()) {
        const size_t offset = static_cast<size_t>(mismatch.first - actual.begin());
        std::cerr << "[失败] " << algorithm.name << " batch=" << testCase.batch << " 输入长度=" << testCase.inputLength
                  << " 输出长度=" << testCase.outputLength << " 消息序号=" << (offset / testCase.outputLength)
                  << " 字节序号=" << (offset % testCase.outputLength)
                  << " 实际值=" << static_cast<unsigned>(*mismatch.first)
                  << " 期望值=" << static_cast<unsigned>(*mismatch.second) << std::endl;
        return false;
    }

    std::cout << "[通过] " << algorithm.name << " batch=" << testCase.batch << " 输入长度=" << testCase.inputLength
              << " 输出长度=" << testCase.outputLength << " 校验和=" << Checksum(actual) << std::endl;
    return true;
}

bool RunCase(const Algorithm& algorithm, const TestCase& testCase, aclrtStream stream)
{
    const auto input = MakeInput(testCase);
    const auto expected = ShakeReference(algorithm, testCase, input);
    std::vector<uint8_t> actual(expected.size(), 0U);
    RunShakeDevice(algorithm, testCase, input, actual, stream);
    return CheckShakeResult(algorithm, testCase, actual, expected);
}

std::vector<TestCase> BuildCases(const Algorithm& algorithm)
{
    const size_t rate = algorithm.rate;
    return {
        {1, 0, 1, 1001U},
        {2, 1, 3, 1002U},
        {3, 3, 31, 1003U},
        {7, 4, 32, 1004U},
        {15, 63, algorithm.fastOutputLength, 1005U},
        {16, 64, algorithm.fastOutputLength, 1006U},
        {17, 65, 33, 1007U},
        {31, rate - 1, rate - 1, 1008U},
        {32, rate, rate, 1009U},
        {33, rate + 1, rate + 1, 1010U},
        {63, 64, algorithm.fastOutputLength, 1011U},
        {64, rate - 1, 1, 1012U},
        {65, 2 * rate - 1, 64, 1013U},
        {207, rate, 32, 1014U},
        {208, rate + 1, 64, 1015U},
        {209, 2 * rate, rate + 1, 1016U},
        {257, 2 * rate + 1, 2 * rate + 3, 1017U},
        {1024, 64, algorithm.fastOutputLength, 1018U},
    };
}

std::vector<TestCase> BuildStressCases(const Algorithm& algorithm)
{
    return {
        {1024, algorithm.rate + 1, algorithm.rate + 1, 20260803U},
        {8192, 64, algorithm.fastOutputLength, 20260804U},
    };
}

bool RunAllCases(bool stressOnly, aclrtStream stream)
{
    bool passed = true;
    if (!stressOnly) {
        for (size_t batch : {1U, 7U, 8U, 9U, 64U, 255U, 256U, 257U, 1024U, 4096U, 8191U, 8192U}) {
            passed = RunKeccakCase(batch, stream) && passed;
        }
    }
    for (const Algorithm& algorithm : {SHAKE128, SHAKE256}) {
        const std::vector<TestCase> cases = stressOnly ? BuildStressCases(algorithm) : BuildCases(algorithm);
        for (const TestCase& testCase : cases) {
            passed = RunCase(algorithm, testCase, stream) && passed;
        }
    }
    return passed;
}

} // namespace

int main(int argc, char** argv)
{
    const int deviceId = argc > 1 ? std::atoi(argv[1]) : 0;
    const bool stressOnly = argc > 2 && std::string(argv[2]) == "--stress-case";

    bool passed = true;
    aclrtStream stream = nullptr;
    try {
        RequireAcl(aclInit(nullptr), "aclInit");
        RequireAcl(aclrtSetDevice(deviceId), "aclrtSetDevice");
        example::CheckDeviceTarget();
        RequireAcl(aclrtCreateStream(&stream), "aclrtCreateStream");

        passed = RunAllCases(stressOnly, stream);

        RequireAcl(aclrtDestroyStream(stream), "aclrtDestroyStream");
        stream = nullptr;
        RequireAcl(aclrtResetDevice(deviceId), "aclrtResetDevice");
        RequireAcl(aclFinalize(), "aclFinalize");
    } catch (const std::exception& error) {
        std::cerr << "[失败] " << error.what() << std::endl;
        passed = false;
        if (stream != nullptr) {
            aclrtDestroyStream(stream);
        }
        aclrtResetDevice(deviceId);
        aclFinalize();
    }

    std::cout << "CORRECTNESS_RESULT=" << (passed ? "PASS" : "FAIL") << std::endl;
    return passed ? 0 : 1;
}
