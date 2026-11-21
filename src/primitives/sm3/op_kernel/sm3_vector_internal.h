/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef CANN_CRYPTO_SM3_VECTOR_INTERNAL_H
#define CANN_CRYPTO_SM3_VECTOR_INTERNAL_H
#include "../../../math/bitops/u32/op_kernel/vector_u32.h"
#include "sm3_words_internal.h"
namespace cann_crypto::sm3::device {
template <uint32_t Q>
struct VectorSm3 {
    AscendC::LocalTensor<uint32_t> storage;
    cann_crypto::math::u32::Vector v;
    __aicore__ inline auto W(uint32_t j) { return storage[j * Q]; }
    __aicore__ inline auto C(uint32_t j) { return W(80 + j); }
    __aicore__ inline auto T(uint32_t j) { return W(88 + j); }
    __aicore__ inline void Reset()
    {
        const uint32_t iv[8] = {0x7380166fU, 0x4914b2b9U, 0x172442d7U, 0xda8a0600U,
                                0xa96f30bcU, 0x163138aaU, 0xe38dee4dU, 0xb0fb0e4eU};
        for (uint32_t j = 0; j < 8; ++j)
            v.Constant(C(j), iv[j]);
    }
    template <bool Late>
    __aicore__ inline void RoundBoolean(
        AscendC::LocalTensor<uint32_t> a, AscendC::LocalTensor<uint32_t> b, AscendC::LocalTensor<uint32_t> c,
        AscendC::LocalTensor<uint32_t> e, AscendC::LocalTensor<uint32_t> f, AscendC::LocalTensor<uint32_t> g)
    {
        if constexpr (!Late) {
            v.Xor(T(3), a, b);
            v.Xor(T(3), T(3), c);
            v.Xor(T(4), e, f);
            v.Xor(T(4), T(4), g);
        } else {
            v.And(T(3), a, b);
            v.Or(T(5), a, b);
            v.And(T(5), T(5), c);
            v.Or(T(3), T(3), T(5));
            v.And(T(4), e, f);
            v.Not(T(5), e);
            v.And(T(5), T(5), g);
            v.Or(T(4), T(4), T(5));
        }
    }

    // The two SM3 Boolean-function phases are public round indices. This
    // specialization removes their per-round control branch, without unrolling.
    template <bool Late, bool Fixed = false>
    __aicore__ inline void Round(
        uint32_t j, uint32_t constant, AscendC::LocalTensor<uint32_t>& a, AscendC::LocalTensor<uint32_t>& b,
        AscendC::LocalTensor<uint32_t>& c, AscendC::LocalTensor<uint32_t>& d, AscendC::LocalTensor<uint32_t>& e,
        AscendC::LocalTensor<uint32_t>& f, AscendC::LocalTensor<uint32_t>& g, AscendC::LocalTensor<uint32_t>& h,
        AscendC::LocalTensor<uint32_t>& na, AscendC::LocalTensor<uint32_t>& ne, AscendC::LocalTensor<uint32_t>& rc,
        AscendC::LocalTensor<uint32_t>& rg, const uint32_t* padding)
    {
        v.Rotl(T(0), a, 12);
        v.Add(T(1), T(0), e);
        v.AddScalar(T(1), T(1), constant);
        v.Rotl(T(1), T(1), 7);
        v.Xor(T(2), T(1), T(0));
        RoundBoolean<Late>(a, b, c, e, f, g);
        v.Add(na, T(3), d);
        v.Add(na, na, T(2));
        if constexpr (Fixed)
            v.AddScalar(na, na, padding[j] ^ padding[j + 4]);
        else {
            v.Xor(T(5), W(j), W(j + 4));
            v.Add(na, na, T(5));
        }
        v.Add(ne, T(4), h);
        v.Add(ne, ne, T(1));
        if constexpr (Fixed)
            v.AddScalar(ne, ne, padding[j]);
        else
            v.Add(ne, ne, W(j));
        v.Permute(ne, ne, 9, 17);
        v.Rotl(rc, b, 9);
        v.Rotl(rg, f, 19);
        auto ob = b, od = d, of = f, oh = h;
        d = c;
        c = rc;
        b = a;
        a = na;
        h = g;
        g = rg;
        f = e;
        e = ne;
        na = od;
        ne = oh;
        rc = ob;
        rg = of;
    }
    template <bool Fixed = false>
    __aicore__ inline void Compress(const uint32_t* padding = nullptr)
    {
        if constexpr (!Fixed)
            for (uint32_t j = 16; j < 68; ++j) {
                v.Rotl(T(0), W(j - 3), 15);
                v.Xor(T(0), T(0), W(j - 9));
                v.Xor(T(0), T(0), W(j - 16));
                v.Permute(T(0), T(0), 15, 23);
                v.Rotl(T(1), W(j - 13), 7);
                v.Xor(T(0), T(0), T(1));
                v.Xor(W(j), T(0), W(j - 6));
            }
        for (uint32_t j = 0; j < 8; ++j)
            v.Copy(W(68 + j), C(j));
        auto a = W(68), b = W(69), c = W(70), d = W(71), e = W(72), f = W(73), g = W(74), h = W(75);
        auto na = W(76), ne = W(77), rc = W(78), rg = W(79);
        uint32_t constant = 0x79cc4519U;
        for (uint32_t j = 0; j < 16; ++j) {
            Round<false, Fixed>(j, constant, a, b, c, d, e, f, g, h, na, ne, rc, rg, padding);
            constant = RotateLeft(constant, 1);
        }
        constant = RotateLeft(0x7a879d8aU, 16);
        for (uint32_t j = 16; j < 64; ++j) {
            Round<true, Fixed>(j, constant, a, b, c, d, e, f, g, h, na, ne, rc, rg, padding);
            constant = RotateLeft(constant, 1);
        }
        v.Xor(C(0), C(0), a);
        v.Xor(C(1), C(1), b);
        v.Xor(C(2), C(2), c);
        v.Xor(C(3), C(3), d);
        v.Xor(C(4), C(4), e);
        v.Xor(C(5), C(5), f);
        v.Xor(C(6), C(6), g);
        v.Xor(C(7), C(7), h);
    }
};
} // namespace cann_crypto::sm3::device
#endif
