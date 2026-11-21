/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef CANN_CRYPTO_SM3_WORDS_INTERNAL_H
#define CANN_CRYPTO_SM3_WORDS_INTERNAL_H
#include "sm3_device_internal.h"
namespace cann_crypto::sm3::device {
template <bool Early>
CANN_CRYPTO_DEVICE_INLINE void WordRound(
    uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d, uint32_t& e, uint32_t& f, uint32_t& g, uint32_t& h, uint32_t w,
    uint32_t wx, uint32_t t)
{
    const uint32_t ar = RotateLeft(a, 12), ss1 = RotateLeft(ar + e + t, 7), ss2 = ss1 ^ ar;
    const uint32_t ff = Early ? (a ^ b ^ c) : ((a & b) | (a & c) | (b & c));
    const uint32_t gg = Early ? (e ^ f ^ g) : ((e & f) | (~e & g));
    const uint32_t na = ff + d + ss2 + wx, ne = gg + h + ss1 + w;
    d = c;
    c = RotateLeft(b, 9);
    b = a;
    a = na;
    h = g;
    g = RotateLeft(f, 19);
    f = e;
    e = Permutation0(ne);
}
CANN_CRYPTO_DEVICE_INLINE void CompressWords(uint32_t* state, uint32_t* w)
{
    for (uint32_t j = 16; j < 68; ++j)
        w[j] = Permutation1(w[j - 16] ^ w[j - 9] ^ RotateLeft(w[j - 3], 15)) ^ RotateLeft(w[j - 13], 7) ^ w[j - 6];
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4], f = state[5], g = state[6],
             h = state[7];
    uint32_t t = 0x79cc4519U;
    for (uint32_t j = 0; j < 16; ++j) {
        WordRound<true>(a, b, c, d, e, f, g, h, w[j], w[j] ^ w[j + 4], t);
        t = RotateLeft(t, 1);
    }
    t = RotateLeft(0x7a879d8aU, 16);
    for (uint32_t j = 16; j < 64; ++j) {
        WordRound<false>(a, b, c, d, e, f, g, h, w[j], w[j] ^ w[j + 4], t);
        t = RotateLeft(t, 1);
    }
    state[0] ^= a;
    state[1] ^= b;
    state[2] ^= c;
    state[3] ^= d;
    state[4] ^= e;
    state[5] ^= f;
    state[6] ^= g;
    state[7] ^= h;
}
CANN_CRYPTO_DEVICE_INLINE uint32_t ByteSwap32(uint32_t w)
{
    return (w << 24) | ((w & 0xff00U) << 8) | ((w >> 8) & 0xff00U) | (w >> 24);
}
CANN_CRYPTO_DEVICE_INLINE void HashWords(
    AscendC::GlobalTensor<uint8_t>& input, AscendC::GlobalTensor<uint32_t>& input32, bool baseAligned, uint32_t offset,
    uint32_t length, volatile uint8_t* out)
{
    uint32_t state[8] = {0x7380166fU, 0x4914b2b9U, 0x172442d7U, 0xda8a0600U,
                         0xa96f30bcU, 0x163138aaU, 0xe38dee4dU, 0xb0fb0e4eU};
    uint32_t words[68]; // Every word is written before its first read.
    const uint32_t full = length / 64;
    for (uint32_t block = 0; block < full; ++block) {
        for (uint32_t j = 0; j < 16; ++j) {
            const uint32_t index = offset + block * 64 + j * 4;
            if (baseAligned && !(offset & 3))
                words[j] = ByteSwap32(input32.GetValue(index / 4));
            else
                words[j] = (uint32_t(input.GetValue(index)) << 24) | (uint32_t(input.GetValue(index + 1)) << 16) |
                           (uint32_t(input.GetValue(index + 2)) << 8) | uint32_t(input.GetValue(index + 3));
        }
        CompressWords(state, words);
    }
    uint32_t tail[32]{};
    const uint32_t remain = length % 64, paddingBlocks = remain >= 56 ? 2 : 1;
    for (uint32_t j = 0; j < remain; ++j)
        tail[j / 4] |= uint32_t(input.GetValue(offset + full * 64 + j)) << (24 - 8 * (j % 4));
    tail[remain / 4] |= 0x80U << (24 - 8 * (remain % 4));
    const uint64_t bits = uint64_t(length) * 8;
    tail[paddingBlocks * 16 - 2] = uint32_t(bits >> 32);
    tail[paddingBlocks * 16 - 1] = uint32_t(bits);
    for (uint32_t block = 0; block < paddingBlocks; ++block) {
        for (uint32_t j = 0; j < 16; ++j)
            words[j] = tail[block * 16 + j];
        CompressWords(state, words);
    }
    for (uint32_t j = 0; j < 8; ++j)
        StoreBigEndian(state[j], out + 4 * j);
}
} // namespace cann_crypto::sm3::device
#endif
