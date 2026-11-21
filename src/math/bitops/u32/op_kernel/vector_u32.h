/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef CANN_CRYPTO_VECTOR_U32_H
#define CANN_CRYPTO_VECTOR_U32_H
#include "kernel_operator.h"
namespace cann_crypto::math::u32 {
// Bit patterns are uint32; signed Add/Sub are vector intrinsics, not overflowing
// C++ signed expressions. Bitwise operations reinterpret the same bytes as u16.
struct Vector {
    AscendC::LocalTensor<uint32_t> scratch;
    uint32_t n;
    __aicore__ inline auto T(uint32_t i) const { return scratch[i * n]; }
    __aicore__ inline void Barrier() { AscendC::PipeBarrier<PIPE_V>(); }
    __aicore__ inline void Add(
        AscendC::LocalTensor<uint32_t> d, AscendC::LocalTensor<uint32_t> a, AscendC::LocalTensor<uint32_t> b)
    {
        if ((n == 64 || n == 128 || n == 256)) {
            AscendC::Add(
                d.ReinterpretCast<int32_t>(), a.ReinterpretCast<int32_t>(), b.ReinterpretCast<int32_t>(), uint64_t(64),
                uint8_t(n / 64), {1, 1, 1, 8, 8, 8});
        } else
            AscendC::Add(d.ReinterpretCast<int32_t>(), a.ReinterpretCast<int32_t>(), b.ReinterpretCast<int32_t>(), n);
        Barrier();
    }
    __aicore__ inline void AddScalar(AscendC::LocalTensor<uint32_t> d, AscendC::LocalTensor<uint32_t> a, uint32_t value)
    {
        if ((n == 64 || n == 128 || n == 256)) {
            AscendC::Adds(
                d.ReinterpretCast<int32_t>(), a.ReinterpretCast<int32_t>(), static_cast<int32_t>(value), uint64_t(64),
                uint8_t(n / 64), {1, 1, 8, 8});
        } else
            AscendC::Adds(d.ReinterpretCast<int32_t>(), a.ReinterpretCast<int32_t>(), static_cast<int32_t>(value), n);
        Barrier();
    }
    __aicore__ inline void Sub(
        AscendC::LocalTensor<uint32_t> d, AscendC::LocalTensor<uint32_t> a, AscendC::LocalTensor<uint32_t> b)
    {
        if ((n == 64 || n == 128 || n == 256)) {
            AscendC::Sub(
                d.ReinterpretCast<int32_t>(), a.ReinterpretCast<int32_t>(), b.ReinterpretCast<int32_t>(), uint64_t(64),
                uint8_t(n / 64), {1, 1, 1, 8, 8, 8});
        } else
            AscendC::Sub(d.ReinterpretCast<int32_t>(), a.ReinterpretCast<int32_t>(), b.ReinterpretCast<int32_t>(), n);
        Barrier();
    }
    __aicore__ inline void And(
        AscendC::LocalTensor<uint32_t> d, AscendC::LocalTensor<uint32_t> a, AscendC::LocalTensor<uint32_t> b)
    {
        if ((n == 64 || n == 128 || n == 256)) {
            AscendC::And(
                d.ReinterpretCast<uint16_t>(), a.ReinterpretCast<uint16_t>(), b.ReinterpretCast<uint16_t>(),
                uint64_t(128), uint8_t(n / 64), {1, 1, 1, 8, 8, 8});
        } else
            AscendC::And(
                d.ReinterpretCast<uint16_t>(), a.ReinterpretCast<uint16_t>(), b.ReinterpretCast<uint16_t>(), 2 * n);
        Barrier();
    }
    __aicore__ inline void Or(
        AscendC::LocalTensor<uint32_t> d, AscendC::LocalTensor<uint32_t> a, AscendC::LocalTensor<uint32_t> b)
    {
        if ((n == 64 || n == 128 || n == 256)) {
            AscendC::Or(
                d.ReinterpretCast<uint16_t>(), a.ReinterpretCast<uint16_t>(), b.ReinterpretCast<uint16_t>(),
                uint64_t(128), uint8_t(n / 64), {1, 1, 1, 8, 8, 8});
        } else
            AscendC::Or(
                d.ReinterpretCast<uint16_t>(), a.ReinterpretCast<uint16_t>(), b.ReinterpretCast<uint16_t>(), 2 * n);
        Barrier();
    }
    __aicore__ inline void Not(AscendC::LocalTensor<uint32_t> d, AscendC::LocalTensor<uint32_t> a)
    {
        if ((n == 64 || n == 128 || n == 256)) {
            AscendC::Not(
                d.ReinterpretCast<uint16_t>(), a.ReinterpretCast<uint16_t>(), uint64_t(128), uint8_t(n / 64),
                {1, 1, 8, 8});
        } else
            AscendC::Not(d.ReinterpretCast<uint16_t>(), a.ReinterpretCast<uint16_t>(), 2 * n);
        Barrier();
    }
    __aicore__ inline void Xor(
        AscendC::LocalTensor<uint32_t> d, AscendC::LocalTensor<uint32_t> a, AscendC::LocalTensor<uint32_t> b)
    {
        Or(T(0), a, b);
        And(T(1), a, b);
        Sub(d, T(0), T(1));
    }
    __aicore__ inline void Rotl(AscendC::LocalTensor<uint32_t> d, AscendC::LocalTensor<uint32_t> a, uint32_t r)
    {
        if (!r) {
            if ((n == 64 || n == 128 || n == 256)) {
                AscendC::Adds(
                    d.ReinterpretCast<int32_t>(), a.ReinterpretCast<int32_t>(), int32_t(0), uint64_t(64),
                    uint8_t(n / 64), {1, 1, 8, 8});
            } else
                AscendC::Adds(d.ReinterpretCast<int32_t>(), a.ReinterpretCast<int32_t>(), int32_t(0), n);
            Barrier();
            return;
        }

        if ((n == 64 || n == 128 || n == 256)) {
            AscendC::ShiftLeft(T(2), a, r, uint64_t(64), uint8_t(n / 64), {1, 1, 8, 8});
        } else
            AscendC::ShiftLeft(T(2), a, r, n);
        Barrier();

        if ((n == 64 || n == 128 || n == 256)) {
            AscendC::ShiftRight(T(3), a, uint32_t(32 - r), uint64_t(64), uint8_t(n / 64), {1, 1, 8, 8});
        } else
            AscendC::ShiftRight(T(3), a, uint32_t(32 - r), n);
        Barrier();
        Or(d, T(2), T(3));
    }
    __aicore__ inline void ByteSwap(AscendC::LocalTensor<uint32_t> d, AscendC::LocalTensor<uint32_t> a)
    {
        if ((n == 64 || n == 128 || n == 256)) {
            AscendC::ShiftLeft(
                T(2).ReinterpretCast<uint16_t>(), a.ReinterpretCast<uint16_t>(), uint16_t(8), uint64_t(128),
                uint8_t(n / 64), {1, 1, 8, 8});
        } else
            AscendC::ShiftLeft(T(2).ReinterpretCast<uint16_t>(), a.ReinterpretCast<uint16_t>(), uint16_t(8), 2 * n);
        Barrier();

        if ((n == 64 || n == 128 || n == 256)) {
            AscendC::ShiftRight(
                T(3).ReinterpretCast<uint16_t>(), a.ReinterpretCast<uint16_t>(), uint16_t(8), uint64_t(128),
                uint8_t(n / 64), {1, 1, 8, 8});
        } else
            AscendC::ShiftRight(T(3).ReinterpretCast<uint16_t>(), a.ReinterpretCast<uint16_t>(), uint16_t(8), 2 * n);
        Barrier();
        Or(d, T(2), T(3));
        Rotl(d, d, 16);
    }
    __aicore__ inline void Permute(
        AscendC::LocalTensor<uint32_t> d, AscendC::LocalTensor<uint32_t> a, uint32_t r, uint32_t s)
    {
        Rotl(T(4), a, r);
        Rotl(T(5), a, s);
        Xor(T(4), T(4), T(5));
        Xor(d, a, T(4));
    }
    __aicore__ inline void Constant(AscendC::LocalTensor<uint32_t> d, uint32_t value)
    {
        if ((n == 64 || n == 128 || n == 256)) {
            AscendC::Duplicate(d, value, uint64_t(64), uint8_t(n / 64), uint16_t(1), uint8_t(8));
        } else
            AscendC::Duplicate(d, value, n);
        Barrier();
    }
    __aicore__ inline void Copy(AscendC::LocalTensor<uint32_t> d, AscendC::LocalTensor<uint32_t> a)
    {
        if ((n == 64 || n == 128 || n == 256)) {
            AscendC::Adds(
                d.ReinterpretCast<int32_t>(), a.ReinterpretCast<int32_t>(), int32_t(0), uint64_t(64), uint8_t(n / 64),
                {1, 1, 8, 8});
        } else
            AscendC::Adds(d.ReinterpretCast<int32_t>(), a.ReinterpretCast<int32_t>(), int32_t(0), n);
        Barrier();
    }
};
} // namespace cann_crypto::math::u32
#endif
