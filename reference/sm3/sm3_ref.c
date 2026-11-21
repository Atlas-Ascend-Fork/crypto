/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#include "sm3_ref.h"
#include <stdlib.h>
#include <string.h>

static uint32_t Rotate(uint32_t value, unsigned shift)
{
    return (value << (shift & 31)) | (value >> ((32 - shift) & 31));
}
static uint32_t Read32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
int CannCryptoRefSm3(const uint8_t* input, size_t length, uint8_t output[32])
{
    uint32_t state[8] = {0x7380166fU, 0x4914b2b9U, 0x172442d7U, 0xda8a0600U,
                         0xa96f30bcU, 0x163138aaU, 0xe38dee4dU, 0xb0fb0e4eU};
    uint8_t digest[32];
    if (!output || (!input && length) || length > UINT64_MAX / 8 || length > SIZE_MAX - 72)
        return -1;
    const size_t padded = (length + 72) & ~(size_t)63;
    uint8_t* bytes = (uint8_t*)calloc(padded, 1);
    if (!bytes)
        return -1;
    if (length)
        memcpy(bytes, input, length);
    bytes[length] = 0x80;
    const uint64_t bitLength = (uint64_t)length * 8;
    for (unsigned i = 0; i < 8; ++i)
        bytes[padded - 8 + i] = (uint8_t)(bitLength >> (56 - 8 * i));
    for (size_t block = 0; block < padded; block += 64) {
        uint32_t w[68], expanded[64];
        for (unsigned j = 0; j < 16; ++j)
            w[j] = Read32(bytes + block + 4 * j);
        for (unsigned j = 16; j < 68; ++j) {
            uint32_t value = w[j - 16] ^ w[j - 9] ^ Rotate(w[j - 3], 15);
            w[j] = value ^ Rotate(value, 15) ^ Rotate(value, 23) ^ Rotate(w[j - 13], 7) ^ w[j - 6];
        }
        for (unsigned j = 0; j < 64; ++j)
            expanded[j] = w[j] ^ w[j + 4];
        uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4], f = state[5], g = state[6],
                 h = state[7];
        for (unsigned j = 0; j < 64; ++j) {
            const uint32_t t = j < 16 ? 0x79cc4519U : 0x7a879d8aU;
            const uint32_t ss1 = Rotate(Rotate(a, 12) + e + Rotate(t, j), 7);
            const uint32_t ss2 = ss1 ^ Rotate(a, 12);
            const uint32_t ff = j < 16 ? (a ^ b ^ c) : ((a & b) | (a & c) | (b & c));
            const uint32_t gg = j < 16 ? (e ^ f ^ g) : ((e & f) | ((~e) & g));
            const uint32_t tt1 = ff + d + ss2 + expanded[j];
            const uint32_t tt2 = gg + h + ss1 + w[j];
            d = c;
            c = Rotate(b, 9);
            b = a;
            a = tt1;
            h = g;
            g = Rotate(f, 19);
            f = e;
            e = tt2 ^ Rotate(tt2, 9) ^ Rotate(tt2, 17);
        }
        const uint32_t compressed[8] = {a, b, c, d, e, f, g, h};
        for (unsigned j = 0; j < 8; ++j)
            state[j] ^= compressed[j];
    }
    free(bytes);
    for (unsigned i = 0; i < 8; ++i)
        for (unsigned j = 0; j < 4; ++j)
            digest[4 * i + j] = (uint8_t)(state[i] >> (24 - 8 * j));
    memcpy(output, digest, 32);
    return 0;
}
