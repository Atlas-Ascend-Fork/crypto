/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef __NTT_VEC_HPP__
#define __NTT_VEC_HPP__
#include "kernel_operator.h"
#include "ntt_macros.h"
#include <cstdint>

struct Tensor_int8x4 {
    LocalTensor<int8_t>& x0;
    LocalTensor<int8_t>& x1;
    LocalTensor<int8_t>& x2;
    LocalTensor<int8_t>& x3;
};

struct Tensor_int8x2 {
    LocalTensor<int8_t>& x0;
    LocalTensor<int8_t>& x1;
};

struct Tensor_int8x3 {
    LocalTensor<int8_t>& x0;
    LocalTensor<int8_t>& x1;
    LocalTensor<int8_t>& x2;
};

template <typename U, typename T>
__aicore__ static inline auto tr(LocalTensor<T> x)
{
    return x.template ReinterpretCast<U>();
}

// __aicore__ static inline void split_4xint7(int8_t &d0, int8_t &d1, int8_t &d2, int8_t &d3, int32_t a) {
//     d0 = a & 0x7f;
//     d1 = (a >> 7) & 0x7f;
//     d2 = (a >> 14) & 0x7f;
//     d3 = (a >> 21) & 0x7f;
//     // __assertion_info("%x %x %x %x <- %x", d0, d1, d2, d3, a);
// }

__aicore__ static inline void split_2xint7_scalar(int8_t& d0, int8_t& d1, int32_t a)
{
    d0 = a & 0x7f;
    d1 = (a >> 7) & 0x7f;
    // d2 = (a >> 14) & 0x7f;
    // d3 = (a >> 21) & 0x7f;
    // __assertion_info("%x %x %x %x <- %x", d0, d1, d2, d3, a);
}

__aicore__ inline void split_vec_int7x4(
    Tensor_int8x4& dst, LocalTensor<int32_t>& src, LocalTensor<int32_t>& t1, LocalTensor<half>& t2, const int32_t count)
{
    AscendC::SetDeqScale((half)1.0f);

    AscendC::ShiftRight(tr<uint32_t>(t1), tr<uint32_t>(src), (uint32_t)(7 * 3), count);
    AscendC::Cast(t2, t1, AscendC::RoundMode::CAST_NONE, count);
    AscendC::Cast(dst.x3, t2, AscendC::RoundMode::CAST_NONE, count);

    AscendC::ShiftLeft(t1, t1, 7 * 3, count);
    AscendC::Sub(src, src, t1, count);
    AscendC::ShiftRight(tr<uint32_t>(t1), tr<uint32_t>(src), (uint32_t)(7 * 2), count);
    AscendC::Cast(t2, t1, AscendC::RoundMode::CAST_NONE, count);
    AscendC::Cast(dst.x2, t2, AscendC::RoundMode::CAST_NONE, count);

    AscendC::ShiftLeft(t1, t1, 7 * 2, count);
    AscendC::Sub(src, src, t1, count);
    AscendC::ShiftRight(tr<uint32_t>(t1), tr<uint32_t>(src), (uint32_t)(7 * 1), count);
    AscendC::Cast(t2, t1, AscendC::RoundMode::CAST_NONE, count);
    AscendC::Cast(dst.x1, t2, AscendC::RoundMode::CAST_NONE, count);

    AscendC::ShiftLeft(t1, t1, 7 * 1, count);
    AscendC::Sub(t1, src, t1, count);
    AscendC::Cast(t2, t1, AscendC::RoundMode::CAST_NONE, count);
    AscendC::Cast(dst.x0, t2, AscendC::RoundMode::CAST_NONE, count);
}

__aicore__ inline void split_vec_int7x2(
    Tensor_int8x2& dst, LocalTensor<int32_t>& src,
    LocalTensor<int32_t>& t1, LocalTensor<half>& t2,
    const int32_t count)
{
    AscendC::SetDeqScale((half)1.0f);
    AscendC::ShiftRight(
        tr<uint32_t>(t1), tr<uint32_t>(src), (uint32_t)7, count);
    AscendC::Cast(t2, t1, AscendC::RoundMode::CAST_NONE, count);
    AscendC::Cast(dst.x1, t2, AscendC::RoundMode::CAST_NONE, count);
    AscendC::ShiftLeft(t1, t1, 7, count);
    AscendC::Sub(t1, src, t1, count);
    AscendC::Cast(t2, t1, AscendC::RoundMode::CAST_NONE, count);
    AscendC::Cast(dst.x0, t2, AscendC::RoundMode::CAST_NONE, count);
}

__aicore__ inline void split_vec_int8x3_balanced(
    Tensor_int8x3& dst, LocalTensor<int32_t>& src,
    LocalTensor<int32_t>& quotient, LocalTensor<int32_t>& tmp,
    LocalTensor<half>& castTmp, const int32_t count)
{
    AscendC::SetDeqScale((half)1.0f);
    AscendC::Adds(tmp, src, -8355712, count);
    AscendC::ShiftRight(tmp, tmp, (int32_t)31, count);
    AscendC::Adds(tmp, tmp, 1, count);
    AscendC::Muls(tmp, tmp, 8380417, count);
    AscendC::Sub(src, src, tmp, count);

    AscendC::Adds(quotient, src, 128, count);
    AscendC::ShiftRight(quotient, quotient, (int32_t)8, count);
    AscendC::ShiftLeft(tmp, quotient, (int32_t)8, count);
    AscendC::Sub(src, src, tmp, count);
    AscendC::Cast(castTmp, src, AscendC::RoundMode::CAST_NONE, count);
    AscendC::Cast(dst.x0, castTmp, AscendC::RoundMode::CAST_NONE, count);

    AscendC::Adds(tmp, quotient, 128, count);
    AscendC::ShiftRight(tmp, tmp, (int32_t)8, count);
    AscendC::ShiftLeft(src, tmp, (int32_t)8, count);
    AscendC::Sub(quotient, quotient, src, count);
    AscendC::Cast(castTmp, quotient, AscendC::RoundMode::CAST_NONE, count);
    AscendC::Cast(dst.x1, castTmp, AscendC::RoundMode::CAST_NONE, count);
    AscendC::Cast(castTmp, tmp, AscendC::RoundMode::CAST_NONE, count);
    AscendC::Cast(dst.x2, castTmp, AscendC::RoundMode::CAST_NONE, count);
}

__aicore__ inline void ShiftRightU(
    LocalTensor<int32_t>& dst, LocalTensor<int32_t>& src, int32_t scalar, const int32_t count)
{
    AscendC::ShiftRight(tr<uint32_t>(dst), tr<uint32_t>(src), static_cast<uint32_t>(scalar), count);
}

__aicore__ inline void wrap_mod_vec_runtime(
    LocalTensor<int32_t>& dst, LocalTensor<int32_t>& src, int32_t q, LocalTensor<int32_t>& t1, LocalTensor<int32_t>& t2,
    int32_t count)
{
    AscendC::Adds(t1, src, -q, count);
    auto& t1_u32 = *reinterpret_cast<LocalTensor<uint32_t>*>(&t1);
    auto& t2_u32 = *reinterpret_cast<LocalTensor<uint32_t>*>(&t2);
    AscendC::ShiftRight(t2_u32, t1_u32, 31U, count);
    AscendC::Mul(t2, src, t2, count);
    AscendC::Max(dst, t1, t2, count);
}

__aicore__ inline void barrett_mul_vec_runtime(
    LocalTensor<int32_t>& dst, int32_t q, int32_t k, int32_t mu, LocalTensor<int32_t>& t1, LocalTensor<int32_t>& t2,
    int32_t count)
{
    AscendC::ShiftRight(t1, dst, (int32_t)(k - 1), count);
    AscendC::Muls(t1, t1, mu, count);
    AscendC::ShiftRight(t1, t1, (int32_t)(k + 1), count);
    AscendC::Muls(t1, t1, q, count);
    AscendC::Sub(dst, dst, t1, count);
    wrap_mod_vec_runtime(dst, dst, q, t1, t2, count);
}

__aicore__ inline void barrett_mul_small_vecvec_runtime(
    LocalTensor<int32_t>& dst, LocalTensor<int32_t>& src, LocalTensor<int32_t>& w, int32_t q, int32_t k, int32_t mu,
    LocalTensor<int32_t>& t1, LocalTensor<int32_t>& t2, LocalTensor<int32_t>& t3, int32_t count)
{
    AscendC::Mul(t3, src, w, count);
    AscendC::ShiftRight(t1, t3, (int32_t)(k - 2), count);
    AscendC::Muls(t1, t1, mu, count);
    AscendC::ShiftRight(t1, t1, (int32_t)(k + 3), count);
    AscendC::Muls(t1, t1, q, count);
    AscendC::Sub(t3, t3, t1, count);
    wrap_mod_vec_runtime(dst, t3, q, t1, t2, count);
}

__aicore__ inline void barrett_mul_vecvec_runtime(
    LocalTensor<int32_t>& dst, LocalTensor<int32_t>& src, LocalTensor<int32_t>& w, int32_t q, int32_t k, int32_t mu,
    LocalTensor<int32_t>& t1, LocalTensor<int32_t>& t2, LocalTensor<int32_t>& wLo, LocalTensor<int32_t>& wHi,
    LocalTensor<int32_t>& t5, LocalTensor<int32_t>& t6, int32_t count)
{
    if (q < (1 << 16)) {
        barrett_mul_small_vecvec_runtime(dst, src, w, q, k, mu, t1, t2, t5, count);
        return;
    }

    using AscendC::Add;
    using AscendC::ShiftLeft;
    using AscendC::Sub;

    AscendC::Mul(t6, src, w, count);

    ShiftRightU(t2, src, 15, count);
    ShiftLeft(t1, t2, 15, count);
    Sub(t1, src, t1, count);

    ShiftRightU(wHi, w, 15, count);
    ShiftLeft(wLo, wHi, 15, count);
    Sub(wLo, w, wLo, count);

    AscendC::Mul(t5, t1, wLo, count);
    ShiftRightU(t5, t5, 15, count);
    AscendC::Mul(t1, t1, wHi, count);
    Add(t5, t5, t1, count);
    AscendC::Mul(t1, t2, wLo, count);
    Add(t5, t5, t1, count);
    ShiftRightU(t5, t5, (k - 2) - 15, count);
    AscendC::Mul(t1, t2, wHi, count);
    if (k - 2 > 30) {
        ShiftRightU(t1, t1, (k - 2) - 30, count);
    } else if (k - 2 < 30) {
        ShiftLeft(t1, t1, 30 - (k - 2), count);
    }
    Add(t5, t5, t1, count);

    const int32_t muLo = mu & ((1 << 15) - 1);
    const int32_t muHi = mu >> 15;
    ShiftRightU(t2, t5, 15, count);
    ShiftLeft(t1, t2, 15, count);
    Sub(t1, t5, t1, count);
    AscendC::Muls(t5, t1, muLo, count);
    ShiftRightU(t5, t5, 15, count);
    AscendC::Muls(t1, t1, muHi, count);
    Add(t5, t5, t1, count);
    AscendC::Muls(t1, t2, muLo, count);
    Add(t5, t5, t1, count);
    ShiftRightU(t5, t5, (k + 3) - 15, count);
    AscendC::Muls(t1, t2, muHi, count);
    if (k + 3 > 30) {
        ShiftRightU(t1, t1, (k + 3) - 30, count);
    } else if (k + 3 < 30) {
        ShiftLeft(t1, t1, 30 - (k + 3), count);
    }
    Add(t5, t5, t1, count);

    AscendC::Muls(t1, t5, q, count);
    Sub(t5, t6, t1, count);
    ShiftLeft(t5, t5, 1, count);
    ShiftRightU(t5, t5, 1, count);
    wrap_mod_vec_runtime(dst, t5, q, t1, t2, count);
}

#endif
