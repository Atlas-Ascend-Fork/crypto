/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "cann_crypto/aice.h"
#include "aice_cpu_batch.h"
#include "acl/acl.h"

#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
bool selfTest = false;

#define CHECK_ACL(call)                                                                                          \
    do {                                                                                                         \
        const aclError status = (call);                                                                          \
        if (status != ACL_ERROR_NONE) {                                                                          \
            std::fprintf(stderr, "%s:%d: %s failed: %d\n", __FILE__, __LINE__, #call, static_cast<int>(status)); \
            return false;                                                                                        \
        }                                                                                                        \
    } while (false)

struct DeviceBuffers {
    std::uint8_t* input{nullptr};
    std::uint8_t* output{nullptr};
    std::uint8_t* key{nullptr};
    std::uint8_t* iv{nullptr};
};

void FreeBuffers(DeviceBuffers* buffers)
{
    if (buffers->iv != nullptr) {
        (void)aclrtFree(buffers->iv);
    }
    if (buffers->key != nullptr) {
        (void)aclrtFree(buffers->key);
    }
    if (buffers->output != nullptr) {
        (void)aclrtFree(buffers->output);
    }
    if (buffers->input != nullptr) {
        (void)aclrtFree(buffers->input);
    }
    *buffers = {};
}

bool AllocateBuffers(std::uint32_t length, DeviceBuffers* buffers)
{
    CHECK_ACL(aclrtMalloc(reinterpret_cast<void**>(&buffers->input), length, ACL_MEM_MALLOC_HUGE_FIRST));
    CHECK_ACL(aclrtMalloc(reinterpret_cast<void**>(&buffers->output), length, ACL_MEM_MALLOC_HUGE_FIRST));
    CHECK_ACL(aclrtMalloc(reinterpret_cast<void**>(&buffers->key), 32, ACL_MEM_MALLOC_HUGE_FIRST));
    CHECK_ACL(aclrtMalloc(reinterpret_cast<void**>(&buffers->iv), 12, ACL_MEM_MALLOC_HUGE_FIRST));
    return true;
}

struct CaseData {
    std::vector<std::uint8_t> key{std::vector<std::uint8_t>(32)};
    std::vector<std::uint8_t> iv{std::vector<std::uint8_t>(12)};
    std::vector<std::uint8_t> plain;
    std::vector<std::uint8_t> golden;
    DeviceBuffers buffers{};
    std::uint32_t batch = 0;
    std::uint32_t dim = 0;
    explicit CaseData(std::uint32_t length) : plain(length), golden(length) {}
    ~CaseData() { FreeBuffers(&buffers); }
    CaseData(const CaseData&) = delete;
    CaseData& operator=(const CaseData&) = delete;
};

bool PrepareCase(CaseData& data, std::uint32_t batch, std::uint32_t dim)
{
    auto& key = data.key;
    auto& iv = data.iv;
    auto& plain = data.plain;
    auto& golden = data.golden;
    auto& buffers = data.buffers;
    data.batch = batch;
    data.dim = dim;
    const auto length = static_cast<std::uint32_t>(plain.size());
    for (std::size_t i = 0; i < key.size(); ++i) {
        key[i] = static_cast<std::uint8_t>(0x13U + 7U * i);
    }
    for (std::size_t i = 0; i < iv.size(); ++i) {
        iv[i] = static_cast<std::uint8_t>(0xf1U + 3U * i);
    }
    for (std::size_t i = 0; i < plain.size(); ++i) {
        plain[i] = static_cast<std::uint8_t>(0x29U + 11U * i);
    }
    if (!selfTest && aice_cpu_batch_crypt(
                         batch, dim, AICE_CPU_ENCRYPT, key.data(), iv.data(), plain.data(), golden.data(), length) !=
                         AICE_CPU_SUCCESS) {
        return false;
    }
    if (!AllocateBuffers(length, &buffers)) {
        FreeBuffers(&buffers);
        return false;
    }
    CHECK_ACL(aclrtMemcpy(buffers.key, 32, key.data(), 32, ACL_MEMCPY_HOST_TO_DEVICE));
    CHECK_ACL(aclrtMemcpy(buffers.iv, 12, iv.data(), 12, ACL_MEMCPY_HOST_TO_DEVICE));
    return true;
}

bool CheckOperation(CaseData& data, aclrtStream stream, bool inPlace, bool encrypting)
{
    const auto& input = encrypting ? data.plain : data.golden;
    const auto& expected = encrypting ? data.golden : data.plain;
    const auto length = input.size();
    const auto& buffers = data.buffers;
    std::vector<std::uint8_t> actual(length);
    CHECK_ACL(aclrtMemcpy(buffers.input, length, input.data(), length, ACL_MEMCPY_HOST_TO_DEVICE));
    auto crypt = encrypting ? CannCryptoAiceEncrypt : CannCryptoAiceDecrypt;
    CHECK_ACL(crypt(
        buffers.input, length, inPlace ? buffers.input : buffers.output, length, buffers.key, buffers.iv, data.batch,
        data.dim, stream));
    CHECK_ACL(aclrtMemcpy(
        actual.data(), length, inPlace ? buffers.input : buffers.output, length, ACL_MEMCPY_DEVICE_TO_HOST));
    if (actual != expected) {
        std::size_t first = 0;
        while (first < length && actual[first] == expected[first]) {
            ++first;
        }
        std::fprintf(
            stderr, "AICE mismatch encrypting=%d length=%zu inplace=%d first=%zu expected=%02x actual=%02x\n",
            static_cast<int>(encrypting), length, static_cast<int>(inPlace), first, expected[first], actual[first]);
        return false;
    }
    return true;
}

bool RunCase(aclrtStream stream, std::uint32_t batch, std::uint32_t dim, std::uint32_t length, bool inPlace)
{
    CaseData data(length);
    if (!PrepareCase(data, batch, dim)) {
        return false;
    }
    if (selfTest) {
        // Functional validation uses device ciphertext for the round trip;
        // the separate cross-test mode uses the independent submission CPU.
        CHECK_ACL(aclrtMemcpy(data.buffers.input, length, data.plain.data(), length, ACL_MEMCPY_HOST_TO_DEVICE));
        CHECK_ACL(CannCryptoAiceEncrypt(
            data.buffers.input, length, data.buffers.output, length, data.buffers.key, data.buffers.iv, batch, dim,
            stream));
        CHECK_ACL(aclrtMemcpy(data.golden.data(), length, data.buffers.output, length, ACL_MEMCPY_DEVICE_TO_HOST));
    }
    if (!CheckOperation(data, stream, inPlace, true) || !CheckOperation(data, stream, inPlace, false)) {
        return false;
    }
    std::printf(
        "AICE_DEVICE_PASS batch=%u dim=%u "
        "length=%u inplace=%d\n",
        batch, dim, length, static_cast<int>(inPlace));
    return true;
}

struct Options {
    bool smoke = false;
    bool tailRegression = false;
};

bool ParseOptions(int argc, char** argv, Options& options)
{
    bool crossTest = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--smoke") == 0)
            options.smoke = true;
        else if (std::strcmp(argv[i], "--tail-regression") == 0)
            options.tailRegression = true;
        else if (std::strcmp(argv[i], "--self-test") == 0)
            selfTest = true;
        else if (std::strcmp(argv[i], "--cross-test") == 0)
            crossTest = true;
        else {
            std::fprintf(stderr, "usage: %s [--self-test|--cross-test] [--smoke|--tail-regression]\n", argv[0]);
            return false;
        }
    }
    if ((selfTest && crossTest) || (options.smoke && options.tailRegression))
        return false;
    return true;
}

bool CheckTileBoundaries(aclrtStream stream, std::uint32_t batch)
{
    const std::uint32_t tileBytes = 64U * batch;
    // Near-capacity padding and queue reuse need both aliasing modes.
    for (std::uint32_t length : {tileBytes - 2U, tileBytes - 1U, tileBytes, 3U * tileBytes + 17U}) {
        for (bool inPlace : {false, true}) {
            if (!RunCase(stream, batch, 1, length, inPlace)) {
                return false;
            }
        }
    }
    return true;
}

bool CheckLaneBoundaries(aclrtStream stream, std::uint32_t batch)
{
    if (batch == 0U) {
        return false;
    }
    const std::uint32_t tileBytes = 64U * batch;
    // Cover the int16 sign boundary and nonzero high word of the lane index.
    // One tile per AIV reaches every lane through 0xFFFF, then a partial tile
    // starts the next group at 0x10000.
    const std::uint32_t laneDim = 65536U / batch + 1U;
    for (bool inPlace : {false, true}) {
        if (!RunCase(stream, batch, laneDim, (laneDim - 1U) * tileBytes + 17U, inPlace)) {
            return false;
        }
    }
    return true;
}

bool CheckLengths(aclrtStream stream, std::uint32_t batch, std::uint32_t smallDim)
{
    const std::uint32_t tileBytes = 64U * batch;
    const std::uint32_t dims[] = {1, smallDim};
    const std::uint32_t lengths[] = {1, 17, tileBytes - 1, tileBytes + 1};
    for (std::uint32_t dim : dims) {
        for (std::uint32_t length : lengths) {
            if (!RunCase(stream, batch, dim, length, false)) {
                return false;
            }
        }
    }
    return true;
}

bool CheckBlockPartition(aclrtStream stream, std::uint32_t batch, std::uint32_t smallDim, std::uint32_t fullDim)
{
    const std::uint32_t tileBytes = 64U * batch;
    // With dim blocks, cover exact division and an uneven division with
    // at least three tiles per block. The final tile has an odd-byte tail.
    for (std::uint32_t dim : {smallDim, fullDim}) {
        for (std::uint32_t length : {3U * dim * tileBytes, (3U * dim + 1U) * tileBytes + 17U}) {
            for (bool inPlace : {false, true}) {
                if (!RunCase(stream, batch, dim, length, inPlace)) {
                    return false;
                }
            }
        }
    }
    return true;
}

bool RunBatch(aclrtStream stream, std::uint32_t batch, std::uint32_t fullDim, const Options& options)
{
    const auto smallDim = fullDim < 5U ? fullDim : 5U;
    const std::uint32_t tileBytes = 64U * batch;
    if (!options.smoke && batch == 128U) {
        // Short inputs in both aliasing modes and the single-byte tail.
        if (!RunCase(stream, batch, 1, 4, false) || !RunCase(stream, batch, 1, 4, true) ||
            !RunCase(stream, batch, 1, 1, false)) {
            return false;
        }
    }

    if (!options.smoke && !CheckTileBoundaries(stream, batch))
        return false;
    if (options.smoke || options.tailRegression) {
        return RunCase(stream, batch, smallDim, 17, true) &&
               RunCase(stream, batch, smallDim, (2U * smallDim + 1U) * tileBytes + 17U, true);
    }
    return CheckLaneBoundaries(stream, batch) && CheckLengths(stream, batch, smallDim) &&
           CheckBlockPartition(stream, batch, smallDim, fullDim) &&
           RunCase(stream, batch, smallDim, tileBytes + 1U, true);
}

} // namespace

int main(int argc, char** argv)
{
    // Flush each completed case while slow simulator runs are redirected to a log.
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    Options options;
    if (!ParseOptions(argc, argv, options))
        return 1;
    aclrtContext context = nullptr;
    aclrtStream stream = nullptr;
    if (aclInit(nullptr) != ACL_ERROR_NONE || aclrtSetDevice(CANN_DEVICE_ID) != ACL_ERROR_NONE ||
        aclrtCreateContext(&context, CANN_DEVICE_ID) != ACL_ERROR_NONE ||
        aclrtCreateStream(&stream) != ACL_ERROR_NONE) {
        std::fprintf(stderr, "failed to initialize ACL runtime\n");
        return 1;
    }

    int64_t coreCount = 0;
    if (aclrtGetDeviceInfo(CANN_DEVICE_ID, ACL_DEV_ATTR_VECTOR_CORE_NUM, &coreCount) != ACL_ERROR_NONE ||
        coreCount <= 0 || coreCount > 1024) {
        std::fprintf(stderr, "invalid device core count for ST\n");
        return 1;
    }
    const auto fullDim = static_cast<std::uint32_t>(coreCount);
    for (std::uint32_t batch : {128U, 256U, 512U}) {
        if (!RunBatch(stream, batch, fullDim, options))
            return 1;
    }
    (void)aclrtDestroyStream(stream);
    (void)aclrtDestroyContext(context);
    (void)aclrtResetDevice(CANN_DEVICE_ID);
    (void)aclFinalize();
    std::puts(
        selfTest      ? "AICE_FUNCTIONAL_SELF_TEST_PASS" :
        options.smoke ? "AICE_DEVICE_SMOKE_PASS" :
                        (options.tailRegression ? "AICE_DEVICE_TAIL_REGRESSION_PASS" : "ALL_AICE_DEVICE_TESTS_PASS"));
    return 0;
}
