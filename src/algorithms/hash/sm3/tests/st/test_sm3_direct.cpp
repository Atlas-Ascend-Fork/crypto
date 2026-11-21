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
#include "sm3_ref.h"
#include "acl/acl.h"
#include <vector>
#include <string>
#if defined(SM3_TEST_VECTOR_RAW)
extern "C" void CannCryptoSm3VectorLaunch(uint32_t, void*, const uint8_t*, uint8_t*, uint32_t, uint32_t);
#endif
#include <array>
#include <random>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#if defined(SM3_ORACLE_OPENSSL)
#include <openssl/evp.h>
#endif
void Check(bool ok, const char* what)
{
    if (!ok)
        throw std::runtime_error(what);
}
struct Buffer {
    void* pointer = nullptr;
    bool pinned;
    Buffer(size_t bytes, bool host = false) : pinned(host)
    {
        Check(
            (host ? aclrtMallocHost(&pointer, bytes) : aclrtMalloc(&pointer, bytes, ACL_MEM_MALLOC_NORMAL_ONLY)) == 0,
            "allocate");
    }
    ~Buffer()
    {
        if (pointer) {
            if (pinned)
                aclrtFreeHost(pointer);
            else
                aclrtFree(pointer);
        }
    }
    void Release()
    {
        Check((pinned ? aclrtFreeHost(pointer) : aclrtFree(pointer)) == 0, "release buffer");
        pointer = nullptr;
    }
};
struct Options {
    bool wideSmoke;
    bool padSmoke;
    bool smoke;
    bool forceVector;
};

bool MatchesSmokeBatch(size_t batch)
{
    return batch == 1 || batch == 31 || batch == 128 || batch == 129 || batch == 2048 || batch == 2049;
}

bool MatchesSmokeLength(size_t length, bool padSmoke)
{
    return length == 0 || length == 55 || length == 56 || length == 64 || length == 65 || length == 1024 ||
           (padSmoke && length == 31);
}

bool ShouldRun(const Options& options, size_t batch, size_t length)
{
    if (options.wideSmoke)
        return (batch == 16383 || batch == 16384 || batch == 16385) &&
               (length == 31 || length == 64 || length == 65 || length == 1024);
    if (options.smoke && (!MatchesSmokeBatch(batch) || !MatchesSmokeLength(length, options.padSmoke)))
        return false;
    if (options.forceVector && batch > 257)
        return false;
    return batch <= 257 || length != 4097 || batch == 2049;
}

void BuildExpected(const uint8_t* data, size_t batch, size_t length, std::vector<uint8_t>& expected)
{
    for (size_t i = 0; i < batch; ++i) {
        Check(CannCryptoRefSm3(data + i * length, length, expected.data() + i * 32) == 0, "C oracle");
#if defined(SM3_ORACLE_OPENSSL)
        std::array<uint8_t, 32> independent{};
        unsigned n = 0;
        Check(
            EVP_Digest(data + i * length, length, independent.data(), &n, EVP_sm3(), nullptr) == 1 && n == 32 &&
                std::equal(independent.begin(), independent.end(), expected.begin() + i * 32),
            "OpenSSL oracle");
#endif
    }
}

void Launch(aclrtStream stream, const uint8_t* input, uint8_t* output, size_t batch, size_t length, bool forceVector)
{
#if defined(SM3_TEST_VECTOR_RAW)
    if (forceVector && batch) {
        CannCryptoSm3VectorLaunch(40, stream, input, output, uint32_t(batch), uint32_t(length));
        return;
    }
#else
    (void)forceVector;
#endif
    Check(CannCryptoSm3(input, output, batch, length, stream) == 0, "native direct API");
}

void RunCase(
    aclrtStream stream, std::mt19937_64& generator, size_t inputOffset, size_t batch, size_t length, bool forceVector)
{
    const size_t inputBytes = batch * length, outputBytes = batch * 32;
    Buffer input(inputBytes + 96), output(outputBytes + 64), pinned(inputBytes + 96, true);
    auto* host = static_cast<uint8_t*>(pinned.pointer);
    std::fill_n(host, inputBytes + 96, 0xa5);
    for (size_t j = 0; j < inputBytes; ++j)
        host[inputOffset + j] = uint8_t(generator());
    std::vector<uint8_t> initial(host, host + inputBytes + 96), actual(outputBytes + 64, 0xa5),
        readback(inputBytes + 96);
    std::vector<uint8_t> expected(outputBytes);
    BuildExpected(host + inputOffset, batch, length, expected);
    Check(
        aclrtMemcpy(output.pointer, actual.size(), actual.data(), actual.size(), ACL_MEMCPY_HOST_TO_DEVICE) == 0,
        "output fill");
    // Deliberately leave this producer pending on the caller's stream.
    Check(
        aclrtMemcpyAsync(
            input.pointer, initial.size(), pinned.pointer, initial.size(), ACL_MEMCPY_HOST_TO_DEVICE, stream) == 0,
        "queued producer");
    Launch(
        stream, static_cast<uint8_t*>(input.pointer) + inputOffset, static_cast<uint8_t*>(output.pointer) + 32, batch,
        length, forceVector);
    Check(aclrtSynchronizeStream(stream) == 0, "drain empty batch producer");
    Check(
        aclrtMemcpy(actual.data(), actual.size(), output.pointer, actual.size(), ACL_MEMCPY_DEVICE_TO_HOST) == 0,
        "output read");
    Check(
        aclrtMemcpy(readback.data(), readback.size(), input.pointer, readback.size(), ACL_MEMCPY_DEVICE_TO_HOST) == 0 &&
            readback == initial,
        "input immutable/guards");
    Check(std::equal(expected.begin(), expected.end(), actual.begin() + 32), "direct digest mismatch");
    for (size_t i = 0; i < 32; ++i)
        Check(actual[i] == 0xa5 && actual[outputBytes + 32 + i] == 0xa5, "device output guards");
    pinned.Release();
    output.Release();
    input.Release();
}

int main(int argc, char** argv)
{
    Check(aclInit(nullptr) == 0, "init");
    Check(aclrtSetDevice(0) == 0, "device");
    aclrtStream stream = nullptr;
    Check(aclrtCreateStream(&stream) == 0, "stream");
    const std::string mode = argc > 1 ? argv[1] : "";
    const Options options{
        mode == "--wide-smoke", mode.find("pad-smoke") != std::string::npos, mode.find("smoke") != std::string::npos,
        mode == "--vector" || mode == "--vector-smoke" || mode == "--vector-pad-smoke"};
    std::mt19937_64 generator(0x534d3303);
    size_t records = 0, cases = 0;
    for (size_t inputOffset : {size_t(32), size_t(33)}) {
        for (size_t batch :
             {size_t(0),    size_t(1),    size_t(3),    size_t(31),    size_t(32),    size_t(33),   size_t(39),
              size_t(40),   size_t(41),   size_t(63),   size_t(64),    size_t(65),    size_t(127),  size_t(128),
              size_t(129),  size_t(255),  size_t(256),  size_t(257),   size_t(2047),  size_t(2048), size_t(2049),
              size_t(4095), size_t(4096), size_t(4097), size_t(16383), size_t(16384), size_t(16385)}) {
            for (size_t length :
                 {size_t(0), size_t(1), size_t(31), size_t(32), size_t(33), size_t(55), size_t(56), size_t(63),
                  size_t(64), size_t(65), size_t(127), size_t(119), size_t(120), size_t(128), size_t(1024),
                  size_t(4097)}) {
                if (!ShouldRun(options, batch, length))
                    continue;
                RunCase(stream, generator, inputOffset, batch, length, options.forceVector);
                records += batch;
                ++cases;
            }
        }
    }
    Check(aclrtDestroyStream(stream) == 0, "destroy stream");
    Check(aclrtResetDevice(0) == 0, "reset");
    Check(aclFinalize() == 0, "finalize");
    std::cout << "Device-resident direct SM3 passed cases=" << cases << " records=" << records
              << " seed=0x534d3303; pending producers, guards, independent C oracle\n";
}
