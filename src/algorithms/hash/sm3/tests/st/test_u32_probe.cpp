/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#include "acl/acl.h"
#include <cstdint>
#include <vector>
#include <stdexcept>
#include <iostream>
extern "C" void CannCryptoSm3U32Probe(void*, const uint32_t*, uint32_t*, uint32_t);
void Check(bool ok)
{
    if (!ok)
        throw std::runtime_error("u32 bit-pattern differential");
}
uint32_t R(uint32_t a, unsigned r) { return (a << r) | (a >> (32 - r)); }
void VerifyResults(const std::vector<uint32_t>& in, const std::vector<uint32_t>& out, uint32_t n)
{
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t a = in[i], b = in[n + i];
        const uint32_t expected[] = {
            a + b,
            a ^ b,
            R(a, 7),
            ~a,
            a & b,
            a | b,
            a ^ R(a, 9) ^ R(a, 17),
            a - b,
            a,
            (a << 24) | ((a & 0xff00U) << 8) | ((a >> 8) & 0xff00U) | (a >> 24),
            a + 0x7a879d8aU,
            a + 0xf4f0c766U};
        for (uint32_t j = 0; j < 12; ++j)
            Check(out[j * n + i] == expected[j]);
    }
}

int main()
{
    Check(aclInit(nullptr) == 0);
    Check(aclrtSetDevice(0) == 0);
    aclrtStream stream;
    Check(aclrtCreateStream(&stream) == 0);
    const uint32_t n = 65536;
    std::vector<uint32_t> in(2 * n), out(12 * n);
    const uint32_t edge[] = {0,           1,           2,           0x7fffffffU, 0x80000000U, 0xffffffffU,
                             0xffff0000U, 0x0000ffffU, 0xaaaaaaaaU, 0x55555555U, 0xfffffffeU, 0x80000001U,
                             0x7ffffffeU, 0x01010101U, 0x80808080U, 0xfefefefeU};
    uint32_t seed = 0x534d3305;
    for (uint32_t i = 0; i < n; ++i) {
        for (uint32_t j = 0; j < 2; ++j) {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            in[j * n + i] = i < 256 ? edge[j ? (i % 16) : (i / 16)] : seed;
        }
    }
    void *input, *output;
    Check(aclrtMalloc(&input, in.size() * 4, ACL_MEM_MALLOC_NORMAL_ONLY) == 0);
    Check(aclrtMalloc(&output, out.size() * 4, ACL_MEM_MALLOC_NORMAL_ONLY) == 0);
    Check(aclrtMemcpy(input, in.size() * 4, in.data(), in.size() * 4, ACL_MEMCPY_HOST_TO_DEVICE) == 0);
    CannCryptoSm3U32Probe(stream, (uint32_t*)input, (uint32_t*)output, n);
    Check(aclrtSynchronizeStream(stream) == 0);
    Check(aclrtMemcpy(out.data(), out.size() * 4, output, out.size() * 4, ACL_MEMCPY_DEVICE_TO_HOST) == 0);
    VerifyResults(in, out, n);
    Check(aclrtFree(output) == 0);
    Check(aclrtFree(input) == 0);
    Check(aclrtDestroyStream(stream) == 0);
    Check(aclrtResetDevice(0) == 0);
    Check(aclFinalize() == 0);
    std::cout << "u32 native wrap/add/sub/xor/and/or/not/rotl/permute alias differential 65536 lanes x 12 outputs "
                 "passed seed=0x534d3305\n";
}
