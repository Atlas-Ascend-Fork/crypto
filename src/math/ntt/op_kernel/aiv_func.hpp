/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef __AIV_FUNC_HPP__
#define __AIV_FUNC_HPP__
#include "kernel_operator.h"
#include "ntt_tiling_data.h"
#include "ntt_macros.h"
#include "cube_math.hpp"

#ifdef ASCENDC_CPU_DEBUG
template <typename T>
__aicore__ inline void _AivGmProbe(GM_ADDR addr, uint32_t tileLength)
{
    AscendC::TPipe pipe;
    AscendC::GlobalTensor<T> gm;
    AscendC::TQue<AscendC::TPosition::VECIN, 1> inQueue;
    constexpr int32_t line = (sizeof(T) == 1) ? 32 : 16;

    pipe.InitBuffer(inQueue, 1, tileLength * sizeof(T));
    LocalTensor<T> local = inQueue.AllocTensor<T>();
    gm.SetGlobalBuffer((__gm__ T*)addr);
    AscendC::DataCopy<T>(local, gm, tileLength);
    {
        for (uint32_t i = 0; i < (uint32_t)tileLength; i++) {
            if constexpr (sizeof(T) > 1) {
                AscendC::printf("%d ", local.GetValue(i));
            } else {
                AscendC::printf("%d ", local.GetValue(i));
            }
            if ((i + 1) % line == 0)
                AscendC::printf("\n");
        }
        AscendC::printf("\n");
    }
    inQueue.FreeTensor(local);
}
#define AivGmProbe(addr, T, tileLength)                                                                           \
    {                                                                                                             \
        AscendC::printf(                                                                                          \
            "[%ld]\033[1;32mgm[<%s> %s] \033[0mlen = %d, %s:%d\n", AscendC::GetBlockIdx(), #T, #addr, tileLength, \
            __FILE__, __LINE__);                                                                                  \
        _AivGmProbe<T>(addr, tileLength);                                                                         \
    }
#else
#define AivGmProbe(addr, T, tileLength) \
    do {                                \
        (void)(addr);                   \
        (void)(tileLength);             \
    } while (0)
#endif

using AscendC::DataCopy;

class AivSplit {
public:
    __aicore__ inline AivSplit(int32_t logicalBlockId, int32_t subBlockId, uint8_t stageID, uint32_t n, int32_t bench)
        : logicalBlockId_(logicalBlockId), subBlockId_(subBlockId), bench(bench), n(n), stageID(stageID)
    {}

    __aicore__ inline void Init(GM_ADDR dst0, GM_ADDR dst1, GM_ADDR dst2, GM_ADDR dst3, GM_ADDR src)
    {
        gm_src.SetGlobalBuffer((__gm__ int32_t*)src, static_cast<size_t>(n) * bench);
        gm_dst0.SetGlobalBuffer((__gm__ int8_t*)dst0, static_cast<size_t>(n) * bench);
        gm_dst1.SetGlobalBuffer((__gm__ int8_t*)dst1, static_cast<size_t>(n) * bench);
        gm_dst2.SetGlobalBuffer((__gm__ int8_t*)dst2, static_cast<size_t>(n) * bench);
        gm_dst3.SetGlobalBuffer((__gm__ int8_t*)dst3, static_cast<size_t>(n) * bench);
        pipe.InitBuffer(in_src, 1, n / 2 * bench * sizeof(int32_t));
        pipe.InitBuffer(out_dst0, 1, n / 2 * bench * sizeof(int8_t));
        pipe.InitBuffer(out_dst1, 1, n / 2 * bench * sizeof(int8_t));
        pipe.InitBuffer(out_dst2, 1, n / 2 * bench * sizeof(int8_t));
        pipe.InitBuffer(out_dst3, 1, n / 2 * bench * sizeof(int8_t));
        pipe.InitBuffer(Buf1, n / 2 * bench * sizeof(int32_t));
        pipe.InitBuffer(Buf2, n / 2 * bench * sizeof(half));
    }

    __aicore__ inline void CopyIn()
    {
        LocalTensor<int32_t> local_src = in_src.AllocTensor<int32_t>();

        AscendC::DataCopy(local_src, gm_src[subBlockId_ * n * bench / 2], n * bench / 2);

        // Bad
        // if (stageID == 0 && logicalBlockId_ == 0 && subBlockId_ == 0) {
        //     AscendC::printf("AivSplit CopyIn local_src\n");
        //     for (int i = 0; i < n / 2; i++) {
        //         AscendC::printf("%d ", local_src.GetValue(i));
        //     }
        //     AscendC::printf("\n");
        // }

        in_src.EnQue(local_src);
    }

    __aicore__ inline void Compute()
    {
        const int32_t tileLength = n / 2 * bench;
        LocalTensor<int32_t> local_src = in_src.DeQue<int32_t>();
        LocalTensor<int8_t> local_dst0 = out_dst0.AllocTensor<int8_t>();
        LocalTensor<int8_t> local_dst1 = out_dst1.AllocTensor<int8_t>();
        LocalTensor<int8_t> local_dst2 = out_dst2.AllocTensor<int8_t>();
        LocalTensor<int8_t> local_dst3 = out_dst3.AllocTensor<int8_t>();
        LocalTensor<int32_t> t1 = Buf1.Get<int32_t>();
        LocalTensor<half> t2 = Buf2.Get<half>();
        Tensor_int8x4 res{local_dst0, local_dst1, local_dst2, local_dst3};

        // __print_tensor_short(local_src, 256, 32);
        split_vec_int7x4(res, local_src, t1, t2, tileLength);

        out_dst0.EnQue<int8_t>(local_dst0);
        out_dst1.EnQue<int8_t>(local_dst1);
        out_dst2.EnQue<int8_t>(local_dst2);
        out_dst3.EnQue<int8_t>(local_dst3);
        in_src.FreeTensor(local_src);
    }

    __aicore__ inline void CopyOut()
    {
        LocalTensor<int8_t> local_dst0 = out_dst0.DeQue<int8_t>();
        LocalTensor<int8_t> local_dst1 = out_dst1.DeQue<int8_t>();
        LocalTensor<int8_t> local_dst2 = out_dst2.DeQue<int8_t>();
        LocalTensor<int8_t> local_dst3 = out_dst3.DeQue<int8_t>();

        AscendC::DataCopy(gm_dst0[subBlockId_ * n * bench / 2], local_dst0, n * bench / 2);
        AscendC::DataCopy(gm_dst1[subBlockId_ * n * bench / 2], local_dst1, n * bench / 2);
        AscendC::DataCopy(gm_dst2[subBlockId_ * n * bench / 2], local_dst2, n * bench / 2);
        AscendC::DataCopy(gm_dst3[subBlockId_ * n * bench / 2], local_dst3, n * bench / 2);

        AscendC::PipeBarrier<PIPE_ALL>();

        out_dst0.FreeTensor(local_dst0);
        out_dst1.FreeTensor(local_dst1);
        out_dst2.FreeTensor(local_dst2);
        out_dst3.FreeTensor(local_dst3);
    }

private:
    const int32_t logicalBlockId_, subBlockId_;
    const uint32_t n, bench;
    const uint8_t stageID;
    AscendC::TPipe pipe;
    AscendC::TBuf<AscendC::TPosition::VECCALC> Buf1, Buf2;
    AscendC::TQue<AscendC::TPosition::VECIN, 1> in_src;
    AscendC::TQue<AscendC::TPosition::VECOUT, 1> out_dst0, out_dst1, out_dst2, out_dst3;

    AscendC::GlobalTensor<int32_t> gm_src;
    AscendC::GlobalTensor<int8_t> gm_dst0, gm_dst1, gm_dst2, gm_dst3;
};

class AivMerge {
public:
    __aicore__ inline AivMerge(int32_t subCoreIdx, uint32_t n, int32_t bench)
        : subCoreIdx(subCoreIdx), bench(bench), n(n)
    {}

    __aicore__ inline void Init(GM_ADDR dst, GM_ADDR src0, GM_ADDR src1, GM_ADDR src2, GM_ADDR src3)
    {
        // __assertion_info("MERGE AIV Core #%d", subCoreIdx);

        gm_src0.SetGlobalBuffer((__gm__ int32_t*)src0, static_cast<size_t>(n) * 4 * bench);
        gm_src1.SetGlobalBuffer((__gm__ int32_t*)src1, static_cast<size_t>(n) * 4 * bench);
        gm_src2.SetGlobalBuffer((__gm__ int32_t*)src2, static_cast<size_t>(n) * 4 * bench);
        gm_src3.SetGlobalBuffer((__gm__ int32_t*)src3, static_cast<size_t>(n) * 4 * bench);
        gm_dst.SetGlobalBuffer((__gm__ int32_t*)dst, static_cast<size_t>(n) * bench);
        pipe.InitBuffer(in_src0, 1, n * 4 * bench * sizeof(int32_t));
        pipe.InitBuffer(in_src1, 1, n * 4 * bench * sizeof(int32_t));
        pipe.InitBuffer(in_src2, 1, n * 4 * bench * sizeof(int32_t));
        pipe.InitBuffer(in_src3, 1, n * 4 * bench * sizeof(int32_t));
        pipe.InitBuffer(out_dst, 1, n / 2 * bench * sizeof(int32_t));
    }

    __aicore__ inline void CopyIn(uint32_t loc = 0)
    {
        LocalTensor<int32_t> local_src0 = in_src0.AllocTensor<int32_t>();
        LocalTensor<int32_t> local_src1 = in_src1.AllocTensor<int32_t>();
        LocalTensor<int32_t> local_src2 = in_src2.AllocTensor<int32_t>();
        LocalTensor<int32_t> local_src3 = in_src3.AllocTensor<int32_t>();
        for (int i = 0; i < 4; i++) {
            DataCopy(local_src0[i * n * bench / 2], gm_src0[subCoreIdx * n * bench / 2 + i * n * bench], n * bench / 2);
            DataCopy(local_src1[i * n * bench / 2], gm_src1[subCoreIdx * n * bench / 2 + i * n * bench], n * bench / 2);
            DataCopy(local_src2[i * n * bench / 2], gm_src2[subCoreIdx * n * bench / 2 + i * n * bench], n * bench / 2);
            DataCopy(local_src3[i * n * bench / 2], gm_src3[subCoreIdx * n * bench / 2 + i * n * bench], n * bench / 2);
        }
        in_src0.EnQue(local_src0);
        in_src1.EnQue(local_src1);
        in_src2.EnQue(local_src2);
        in_src3.EnQue(local_src3);
    }

    __aicore__ inline void ShiftRightU(
        LocalTensor<int32_t>& dst, LocalTensor<int32_t>& src, int32_t scalar, const int32_t count)
    {
        AscendC::ShiftRight(tr<uint32_t>(dst), tr<uint32_t>(src), (uint32_t)scalar, count);
    }

    __aicore__ inline void Compute()
    {
        const int32_t count = n * bench / 2;
        LocalTensor<int32_t> local_src[4] = {
            in_src0.DeQue<int32_t>(), in_src1.DeQue<int32_t>(), in_src2.DeQue<int32_t>(), in_src3.DeQue<int32_t>()};
        LocalTensor<int32_t> local_dst = out_dst.AllocTensor<int32_t>();

        LocalTensor<int32_t> x[4][4];
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                x[i][j] = local_src[i][j * n * bench / 2];
            }
        }
        constexpr int32_t b_mu = 8396807, q = 8380417;
        using AscendC::ShiftLeft, AscendC::Add, AscendC::ShiftRight, AscendC::Muls;
        // 高 28 位，储存在 x[3][3]

#ifdef ASCENDC_CPU_DEBUG
        std::vector<int64_t> tx(count), ty(count);
        for (int i = 0; i < count; i++) {
            tx[i] = x[0][0].GetValue(i);
            tx[i] += int64_t(x[0][1].GetValue(i) + x[1][0].GetValue(i)) << 7;
            tx[i] += int64_t(x[0][2].GetValue(i) + x[1][1].GetValue(i) + x[2][0].GetValue(i)) << 14;
            tx[i] += int64_t(x[0][3].GetValue(i) + x[1][2].GetValue(i) + x[2][1].GetValue(i) + x[3][0].GetValue(i))
                     << 21;
            tx[i] += int64_t(x[1][3].GetValue(i) + x[2][2].GetValue(i) + x[3][1].GetValue(i)) << 28;
            tx[i] += int64_t(x[2][3].GetValue(i) + x[3][2].GetValue(i)) << 35;
            tx[i] += int64_t(x[3][3].GetValue(i)) << 42;
        }
#endif
        Add(x[0][1], x[0][1], x[1][0], count);
        Add(x[0][2], x[0][2], x[1][1], count);
        Add(x[0][2], x[0][2], x[2][0], count);
        Add(x[0][3], x[0][3], x[1][2], count);
        Add(x[0][3], x[0][3], x[2][1], count);
        Add(x[0][3], x[0][3], x[3][0], count);
        Add(x[1][3], x[1][3], x[2][2], count);
        Add(x[1][3], x[1][3], x[3][1], count);
        Add(x[2][3], x[2][3], x[3][2], count);
        // 计算低 28 位 -> a
        auto &a = x[1][0], &b = x[2][1], &t1 = x[2][0];
        ShiftLeft(a, x[0][0], 0 + 4, count);
        ShiftLeft(t1, x[0][1], 7 + 4, count);
        Add(a, a, t1, count);
        ShiftLeft(t1, x[0][2], 14 + 4, count);
        Add(a, a, t1, count);
        ShiftLeft(t1, x[0][3], 21 + 4, count);
        Add(a, a, t1, count);
        ShiftRightU(a, a, 4, count);

        // 计算高位，特别：22位 -> t1
        ShiftRightU(t1, x[0][0], 7, count);
        Add(t1, t1, x[0][1], count);
        ShiftRightU(t1, t1, 7, count);
        Add(t1, t1, x[0][2], count);
        ShiftRightU(t1, t1, 7, count);
        Add(t1, t1, x[0][3], count);
        ShiftRightU(b, t1, 1, count); // 已经达到 22 位高
        ShiftLeft(t1, x[1][3], 28 - 22, count);
        Add(b, b, t1, count);
        ShiftLeft(t1, x[2][3], 35 - 22, count);
        Add(b, b, t1, count);
        ShiftLeft(t1, x[3][3], 42 - 22, count);
        Add(t1, b, t1, count);

#ifdef ASCENDC_CPU_DEBUG
        std::vector<int64_t> ta(count), tb(count);
        for (int i = 0; i < count; i++) {
            ta[i] = tx[i] & ((1L << 28) - 1);
            tb[i] = tx[i] >> 22;
        }
        __check_diff(a, ta, count);
        __check_diff(t1, tb, count);
#endif

        auto& t1_lo = x[2][2];
        auto& t1_hi = x[2][3];
        ShiftRight(t1_hi, t1, 12, count);
        ShiftLeft(t1_lo, t1_hi, 12, count);
        Sub(t1_lo, t1, t1_lo, count);

        constexpr int32_t mu_hi = b_mu >> 12;
        constexpr int32_t mu_lo = b_mu & 0xfff;
        Muls(t1_lo, t1_lo, mu_hi, count);
        Muls(t1, t1_hi, mu_lo, count);
        Add(t1, t1, t1_lo, count);
        ShiftRightU(t1, t1, 12, count);
        Muls(t1_hi, t1_hi, mu_hi, count);
        Add(t1, t1, t1_hi, count);

        Muls(t1, t1, q, count);
        Sub(local_dst, a, t1, count);
        ShiftLeft(local_dst, local_dst, 32 - 27, count);
        ShiftRightU(local_dst, local_dst, 32 - 27, count);
        wrap_mod_vec_runtime(local_dst, local_dst, q, t1_lo, t1_hi, n * bench / 2);
        out_dst.EnQue<int32_t>(local_dst);
        in_src0.FreeTensor(local_src[0]);
        in_src1.FreeTensor(local_src[1]);
        in_src2.FreeTensor(local_src[2]);
        in_src3.FreeTensor(local_src[3]);
    }

    __aicore__ inline void CopyOut()
    {
        LocalTensor<int32_t> local_dst = out_dst.DeQue<int32_t>();
        AscendC::DataCopy(gm_dst[subCoreIdx * n * bench / 2], local_dst, n * bench / 2);
        AscendC::PipeBarrier<PIPE_ALL>();
        out_dst.FreeTensor(local_dst);
    }

private:
    const int32_t subCoreIdx, bench;
    const uint32_t n;
    AscendC::TPipe pipe;
    AscendC::TQue<AscendC::TPosition::VECIN, 1> in_src0, in_src1, in_src2, in_src3;
    AscendC::TQue<AscendC::TPosition::VECOUT, 1> out_dst;

    AscendC::GlobalTensor<int32_t> gm_src0, gm_src1, gm_src2, gm_src3;
    AscendC::GlobalTensor<int32_t> gm_dst;
};

#endif
