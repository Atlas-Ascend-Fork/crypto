/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_CRYPTO_SM3_DEVICE_INTERNAL_H
#define CANN_CRYPTO_SM3_DEVICE_INTERNAL_H
#include "../../../common/device_qualifiers.h"
namespace cann_crypto::sm3::device {

CANN_CRYPTO_DEVICE_INLINE uint32_t RotateLeft(uint32_t value, uint32_t bits) noexcept
{
    bits %= 32;
    return bits == 0 ? value : (value << bits) | (value >> (32 - bits));
}

CANN_CRYPTO_DEVICE_INLINE uint32_t LoadBigEndian(const volatile uint8_t* bytes) noexcept
{
    return (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16) |
           (static_cast<uint32_t>(bytes[2]) << 8) | static_cast<uint32_t>(bytes[3]);
}

CANN_CRYPTO_DEVICE_INLINE void StoreBigEndian(uint32_t value, volatile uint8_t* bytes) noexcept
{
    for (size_t i = 0; i < 4; ++i) {
        bytes[i] = static_cast<uint8_t>(value >> (24 - 8 * i));
    }
}

CANN_CRYPTO_DEVICE_INLINE uint32_t Permutation0(uint32_t value) noexcept
{
    return value ^ RotateLeft(value, 9) ^ RotateLeft(value, 17);
}

CANN_CRYPTO_DEVICE_INLINE uint32_t Permutation1(uint32_t value) noexcept
{
    return value ^ RotateLeft(value, 15) ^ RotateLeft(value, 23);
}

// Internal byte-stream primitive; caller checks length and finalizes exactly once.
struct Context {
    uint32_t state_[8];
    // Volatile byte accesses prevent CANN 9.0 Bisheng from folding the
    // serialization into an unsupported i64 bswap during AI Core lowering.
    volatile uint8_t buffer_[64];
    uint64_t length;
    uint32_t buffered;
    CANN_CRYPTO_DEVICE_INLINE void Reset()
    {
        const uint32_t initial[8] = {0x7380166fU, 0x4914b2b9U, 0x172442d7U, 0xda8a0600U,
                                     0xa96f30bcU, 0x163138aaU, 0xe38dee4dU, 0xb0fb0e4eU};
        for (uint32_t i = 0; i < 8; ++i)
            state_[i] = initial[i];
        length = 0;
        buffered = 0;
    }
    CANN_CRYPTO_DEVICE_INLINE void Compress(const volatile uint8_t* block)
    {
        uint32_t words[68]{};
        for (size_t i = 0; i < 16; ++i) {
            words[i] = LoadBigEndian(block + 4 * i);
        }
        for (size_t i = 16; i < 68; ++i) {
            words[i] = Permutation1(words[i - 16] ^ words[i - 9] ^ RotateLeft(words[i - 3], 15)) ^
                       RotateLeft(words[i - 13], 7) ^ words[i - 6];
        }

        uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
        uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
        for (uint32_t round = 0; round < 64; ++round) {
            const uint32_t constant = round < 16 ? 0x79cc4519U : 0x7a879d8aU;
            const uint32_t rotatedA = RotateLeft(a, 12);
            const uint32_t sum1 = RotateLeft(rotatedA + e + RotateLeft(constant, round), 7);
            const uint32_t sum2 = sum1 ^ rotatedA;
            const uint32_t ff = round < 16 ? a ^ b ^ c : (a & b) | (a & c) | (b & c);
            const uint32_t gg = round < 16 ? e ^ f ^ g : (e & f) | (~e & g);
            const uint32_t nextA = ff + d + sum2 + (words[round] ^ words[round + 4]);
            const uint32_t nextE = gg + h + sum1 + words[round];
            d = c;
            c = RotateLeft(b, 9);
            b = a;
            a = nextA;
            h = g;
            g = RotateLeft(f, 19);
            f = e;
            e = Permutation0(nextE);
        }
        state_[0] ^= a;
        state_[1] ^= b;
        state_[2] ^= c;
        state_[3] ^= d;
        state_[4] ^= e;
        state_[5] ^= f;
        state_[6] ^= g;
        state_[7] ^= h;
    }

    CANN_CRYPTO_DEVICE_INLINE void Push(uint8_t value)
    {
        buffer_[buffered++] = value;
        ++length;
        if (buffered == 64) {
            Compress(buffer_);
            buffered = 0;
        }
    }
    CANN_CRYPTO_DEVICE_INLINE void Final(volatile uint8_t* output)
    {
        const uint64_t bits = length * 8;
        buffer_[buffered++] = 0x80;
        if (buffered > 56) {
            while (buffered < 64)
                buffer_[buffered++] = 0;
            Compress(buffer_);
            buffered = 0;
        }
        while (buffered < 56)
            buffer_[buffered++] = 0;
        for (uint32_t i = 0; i < 8; ++i)
            buffer_[56 + i] = uint8_t(bits >> (56 - 8 * i));
        Compress(buffer_);
        for (uint32_t i = 0; i < 8; ++i)
            StoreBigEndian(state_[i], output + 4 * i);
    }
};
} // namespace cann_crypto::sm3::device
#endif
