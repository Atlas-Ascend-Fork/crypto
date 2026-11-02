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
#include "acl/acl.h"
#include "../../../op_host/aice_launch.h"
#include <cstdint>
#include <cstdio>
#include <limits>
#include <initializer_list>

#define CHECK(x)                                                 \
    do {                                                         \
        if (!(x)) {                                              \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); \
            return 1;                                            \
        }                                                        \
    } while (false)
namespace {
uint8_t g_memory[512]{};
const uint8_t* g_input = g_memory;
uint8_t* g_output = g_memory + 128;
const uint8_t* g_key = g_memory + 256;
const uint8_t* g_iv = g_memory + 320;
void* g_expectedStream = g_memory + 400;
int g_launches = 0;
int g_syncs = 0;
int g_queries = 0;
int32_t g_syncError = 0;
int32_t g_launchError = 0;
uint32_t g_gotBatch = 0;
uint32_t g_gotDim = 0;
uint32_t g_gotLength = 0;
bool g_gotEncrypt = false;
bool g_forwarded = false;
} // namespace
extern "C" aclError aclrtGetDevice(int32_t*)
{
    ++g_queries;
    return 99;
}
extern "C" aclError aclrtGetDeviceInfo(uint32_t, aclrtDevAttr, int64_t*)
{
    ++g_queries;
    return 99;
}
extern "C" aclError aclrtSynchronizeStream(aclrtStream stream)
{
    ++g_syncs;
    return stream == g_expectedStream ? g_syncError : 98;
}
extern "C" int32_t CannCryptoAiceLaunch(
    uint32_t batch, bool encrypt, uint32_t blockDim, void* stream, const uint8_t* src, const uint8_t* k,
    const uint8_t* v, uint8_t* dst, uint32_t length)
{
    ++g_launches;
    g_gotBatch = batch;
    g_gotEncrypt = encrypt;
    g_gotDim = blockDim;
    g_gotLength = length;
    g_forwarded =
        src == g_input && k == g_key && v == g_iv && (dst == g_output || dst == g_input) && stream == g_expectedStream;
    return g_launchError;
}
using Crypt = decltype(&CannCryptoAiceEncrypt);

static int CheckValidCalls(Crypt crypt)
{
    for (uint32_t batch : {128U, 256U, 512U}) {
        // Even for a single partial tile, dim must not be clamped to the
        // input size or replaced by a queried physical core count.
        for (uint32_t dim : {1U, 3U, 5U, 48U, 49U}) {
            const int launchesBefore = g_launches;
            const int syncsBefore = g_syncs;
            CHECK(crypt(g_input, 17, g_output, 17, g_key, g_iv, batch, dim, g_expectedStream) == ACL_SUCCESS);
            CHECK(g_forwarded && g_gotBatch == batch && g_gotLength == 17 && g_gotDim == dim);
            CHECK(g_launches == launchesBefore + 1 && g_syncs == syncsBefore + 1 && g_queries == 0);
            CHECK(g_gotEncrypt == (crypt == CannCryptoAiceEncrypt));
        }
        CHECK(crypt(g_input, 17, g_memory, 17, g_key, g_iv, batch, 5, g_expectedStream) == ACL_SUCCESS);
    }
    return 0;
}

static int CheckInvalidArguments(Crypt crypt)
{
    const int before = g_launches;
    CHECK(crypt(g_input, 17, g_output, 16, g_key, g_iv, 128, 1, g_expectedStream) == ACL_ERROR_INVALID_PARAM);
    CHECK(crypt(nullptr, 17, g_output, 17, g_key, g_iv, 128, 1, g_expectedStream) == ACL_ERROR_INVALID_PARAM);
    CHECK(crypt(g_input, 17, nullptr, 17, g_key, g_iv, 128, 1, g_expectedStream) == ACL_ERROR_INVALID_PARAM);
    CHECK(crypt(g_input, 17, g_output, 17, nullptr, g_iv, 128, 1, g_expectedStream) == ACL_ERROR_INVALID_PARAM);
    CHECK(crypt(g_input, 17, g_output, 17, g_key, nullptr, 128, 1, g_expectedStream) == ACL_ERROR_INVALID_PARAM);
    CHECK(crypt(g_input, 17, g_output, 17, g_key, g_iv, 129, 1, g_expectedStream) == ACL_ERROR_INVALID_PARAM);
    CHECK(crypt(g_input, 17, g_output, 17, g_key, g_iv, 128, 0, g_expectedStream) == ACL_ERROR_INVALID_PARAM);
    CHECK(crypt(g_input, 17, g_output, 17, g_key, g_iv, 512, UINT32_MAX, g_expectedStream) == ACL_ERROR_INVALID_PARAM);
    CHECK(
        crypt(g_input, UINT64_MAX, g_output, UINT64_MAX, g_key, g_iv, 128, 1, g_expectedStream) ==
        ACL_ERROR_INVALID_PARAM);
    CHECK(crypt(g_input, 17, g_memory + 1, 17, g_key, g_iv, 128, 1, g_expectedStream) == ACL_ERROR_INVALID_PARAM);
    CHECK(crypt(g_memory + 1, 17, g_memory, 17, g_key, g_iv, 128, 1, g_expectedStream) == ACL_ERROR_INVALID_PARAM);
    CHECK(crypt(g_input, 17, g_memory + 256, 17, g_key, g_iv, 128, 1, g_expectedStream) == ACL_ERROR_INVALID_PARAM);
    CHECK(crypt(g_input, 17, g_memory + 320, 17, g_key, g_iv, 128, 1, g_expectedStream) == ACL_ERROR_INVALID_PARAM);
    auto bad = reinterpret_cast<uint8_t*>(std::numeric_limits<uintptr_t>::max() - 7);
    CHECK(crypt(bad, 17, g_output, 17, g_key, g_iv, 128, 1, g_expectedStream) == ACL_ERROR_INVALID_PARAM);
    CHECK(crypt(g_input, 17, bad, 17, g_key, g_iv, 128, 1, g_expectedStream) == ACL_ERROR_INVALID_PARAM);
    CHECK(crypt(g_input, 17, g_output, 17, bad, g_iv, 128, 1, g_expectedStream) == ACL_ERROR_INVALID_PARAM);
    CHECK(crypt(g_input, 17, g_output, 17, g_key, bad, 128, 1, g_expectedStream) == ACL_ERROR_INVALID_PARAM);
    CHECK(g_launches == before);
    return 0;
}

static int CheckSynchronization(Crypt crypt)
{
    const int before = g_launches;
    const int queriesBefore = g_queries;
    const int syncsBefore = g_syncs;
    CHECK(crypt(nullptr, 0, nullptr, 0, nullptr, nullptr, 128, 1, g_expectedStream) == ACL_SUCCESS);
    CHECK(g_launches == before && g_queries == queriesBefore && g_syncs == syncsBefore + 1);
    g_syncError = 73;
    CHECK(crypt(g_input, 17, g_output, 17, g_key, g_iv, 128, 1, g_expectedStream) == 73);
    g_syncError = 0;
    g_launchError = 74;
    const int pendingSync = g_syncs;
    CHECK(crypt(g_input, 17, g_output, 17, g_key, g_iv, 128, 1, g_expectedStream) == 74);
    CHECK(g_syncs == pendingSync + 1);
    g_syncError = 75;
    CHECK(crypt(g_input, 17, g_output, 17, g_key, g_iv, 128, 1, g_expectedStream) == 74);
    g_syncError = 0;
    g_launchError = 0;
    return 0;
}

static int CheckIvBoundary(Crypt crypt)
{
    // IV occupies exactly [320, 332): its final byte must remain protected,
    // while output immediately after the 12-byte IV must be accepted.
    CHECK(crypt(g_input, 1, g_memory + 331, 1, g_key, g_iv, 128, 1, g_expectedStream) == ACL_ERROR_INVALID_PARAM);
    CHECK(crypt(g_input, 1, g_memory + 332, 1, g_key, g_iv, 128, 1, g_expectedStream) == ACL_SUCCESS);
    return 0;
}

static int CheckLengthLimits()
{
    g_expectedStream = nullptr;
    CHECK(CannCryptoAiceEncrypt(g_input, 17, g_output, 17, g_key, g_iv, 128, 1, nullptr) == ACL_SUCCESS);
    CHECK(g_forwarded);
    // Validate the inclusive 32-bit limit without allocating or dereferencing GM.
    const auto largeInput = reinterpret_cast<const uint8_t*>(uintptr_t{0x100000000ULL});
    auto largeOutput = reinterpret_cast<uint8_t*>(uintptr_t{0x300000000ULL});
    CHECK(
        CannCryptoAiceEncrypt(
            largeInput, UINT32_MAX, largeOutput, UINT32_MAX, g_key, g_iv, 128, UINT32_MAX / 128, nullptr) ==
        ACL_SUCCESS);
    CHECK(g_gotLength == UINT32_MAX && g_gotDim == UINT32_MAX / 128);
    const int beforeOverflow = g_launches;
    CHECK(
        CannCryptoAiceEncrypt(
            largeInput, uint64_t(UINT32_MAX) + 1, largeOutput, uint64_t(UINT32_MAX) + 1, g_key, g_iv, 128, 1,
            nullptr) == ACL_ERROR_INVALID_PARAM);
    CHECK(g_launches == beforeOverflow);
    CHECK(g_queries == 0);
    return 0;
}

int main()
{
    for (auto crypt : {CannCryptoAiceEncrypt, CannCryptoAiceDecrypt}) {
        CHECK(CheckValidCalls(crypt) == 0);
        CHECK(CheckInvalidArguments(crypt) == 0);
        CHECK(CheckSynchronization(crypt) == 0);
        CHECK(CheckIvBoundary(crypt) == 0);
    }
    CHECK(CheckLengthLimits() == 0);
    std::puts("AICE_HOST_API_PASS");
    return 0;
}
