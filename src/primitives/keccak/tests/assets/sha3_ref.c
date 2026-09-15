/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/*
 * Algorithm reference: NIST FIPS 202, Sections 3, 4, and 6.2.
 * SHA-3 Standard: Permutation-Based Hash and Extendable-Output Functions.
 * https://doi.org/10.6028/NIST.FIPS.202
 */
#include "sha3_ref.h"

#include <string.h>



static const uint64_t RC[24] = {
    0x0000000000000001ULL, 0x0000000000008082ULL,
    0x800000000000808aULL, 0x8000000080008000ULL,
    0x000000000000808bULL, 0x0000000080000001ULL,
    0x8000000080008081ULL, 0x8000000000008009ULL,
    0x000000000000008aULL, 0x0000000000000088ULL,
    0x0000000080008009ULL, 0x000000008000000aULL,
    0x000000008000808bULL, 0x800000000000008bULL,
    0x8000000000008089ULL, 0x8000000000008003ULL,
    0x8000000000008002ULL, 0x8000000000000080ULL,
    0x000000000000800aULL, 0x800000008000000aULL,
    0x8000000080008081ULL, 0x8000000000008080ULL,
    0x0000000080000001ULL, 0x8000000080008008ULL,
};

static const int RHO[25] = {
     0,  1, 62, 28, 27,
    36, 44,  6, 55, 20,
     3, 10, 43, 25, 39,
    41, 45, 15, 21,  8,
    18,  2, 61, 56, 14,
};

static uint64_t rotl64(uint64_t x, int n) {
    return n == 0 ? x : ((x << n) | (x >> (64 - n)));
}


uint64_t CannCryptoRefLoad64Le(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v |= (uint64_t)p[i] << (8 * i);
    }
    return v;
}

void CannCryptoRefStore64Le(uint8_t* p, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        p[i] = (uint8_t)((v >> (8 * i)) & 0xffU);
    }
}

void CannCryptoRefKeccakF1600(uint64_t a[25]) {
    for (int round = 0; round < 24; ++round) {
        uint64_t c[5];
        uint64_t d[5];
        uint64_t b[25];

        for (int x = 0; x < 5; ++x) {
            c[x] = a[x] ^ a[x + 5] ^ a[x + 10] ^ a[x + 15] ^ a[x + 20];
        }
        for (int x = 0; x < 5; ++x) {
            d[x] = c[(x + 4) % 5] ^ rotl64(c[(x + 1) % 5], 1);
        }
        for (int y = 0; y < 5; ++y) {
            for (int x = 0; x < 5; ++x) {
                a[x + 5 * y] ^= d[x];
            }
        }

        for (int y = 0; y < 5; ++y) {
            for (int x = 0; x < 5; ++x) {
                const int src = x + 5 * y;
                const int dst = y + 5 * ((2 * x + 3 * y) % 5);
                b[dst] = rotl64(a[src], RHO[src]);
            }
        }

        for (int y = 0; y < 5; ++y) {
            for (int x = 0; x < 5; ++x) {
                a[x + 5 * y] =
                    b[x + 5 * y] ^
                    ((~b[((x + 1) % 5) + 5 * y]) & b[((x + 2) % 5) + 5 * y]);
            }
        }

        a[0] ^= RC[round];
    }
}

/* 按小端字节异或到状态，避免未对齐访问和宿主端序依赖。 */
static void XorBytes(uint64_t state[25], const uint8_t *bytes, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        state[i / 8U] ^= (uint64_t)bytes[i] << (8U * (i % 8U));
    }
}

int CannCryptoRefShake(uint32_t algorithm, const uint8_t *input, size_t inputLength,
    uint8_t *output, size_t outputLength)
{
    if ((algorithm != 128U && algorithm != 256U) ||
        (inputLength != 0U && input == NULL) || output == NULL || outputLength == 0U) {
        return -1;
    }
    const size_t rate = algorithm == 128U ? 168U : 136U;
    uint64_t state[25] = {0};
    size_t offset = 0U;
    while (inputLength - offset >= rate) {
        XorBytes(state, input + offset, rate);
        CannCryptoRefKeccakF1600(state);
        offset += rate;
    }
    const size_t remainder = inputLength - offset;
    if (remainder != 0U) {
        XorBytes(state, input + offset, remainder);
    }
    /* 整块输入也需要额外的填充块；空输入走同一条路径。 */
    state[remainder / 8U] ^= UINT64_C(0x1f) << (8U * (remainder % 8U));
    state[(rate - 1U) / 8U] ^= UINT64_C(0x80) << (8U * ((rate - 1U) % 8U));
    CannCryptoRefKeccakF1600(state);
    size_t produced = 0U;
    while (produced < outputLength) {
        size_t chunk = outputLength - produced;
        if (chunk > rate) {
            chunk = rate;
        }
        for (size_t i = 0; i < chunk; ++i) {
            output[produced + i] = (uint8_t)(state[i / 8U] >> (8U * (i % 8U)));
        }
        produced += chunk;
        if (produced < outputLength) {
            CannCryptoRefKeccakF1600(state);
        }
    }
    return 0;
}
