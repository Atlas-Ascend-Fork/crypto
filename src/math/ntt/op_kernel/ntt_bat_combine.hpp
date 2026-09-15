/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#pragma once

#include "ntt_device_math.hpp"

class BatAivCombine4 {
public:
    __aicore__ inline BatAivCombine4(
        int32_t subCoreIdx, uint32_t n, int32_t bench, int32_t q, int32_t barrettK, uint64_t barrettMu, int32_t fold28)
        : subCoreIdx_(subCoreIdx),
          bench_(bench),
          n_(n),
          q_(q),
          barrettK_(barrettK),
          barrettMu_(static_cast<int32_t>(barrettMu)),
          fold28_(fold28),
          fastFold_(CanUseBase7FoldWithAdd(fold28, static_cast<int32_t>(n)))
    {}

    __aicore__ inline void Init(
        __gm__ int32_t* dstBase, int32_t dstStride, int32_t blockBenchOffset, int32_t actualBench, int32_t logicalN,
        GM_ADDR src0, GM_ADDR src1, GM_ADDR src2, GM_ADDR src3)
    {
        gmSrc0_.SetGlobalBuffer((__gm__ int32_t*)src0, static_cast<size_t>(n_) * bench_);
        gmSrc1_.SetGlobalBuffer((__gm__ int32_t*)src1, static_cast<size_t>(n_) * bench_);
        gmSrc2_.SetGlobalBuffer((__gm__ int32_t*)src2, static_cast<size_t>(n_) * bench_);
        gmSrc3_.SetGlobalBuffer((__gm__ int32_t*)src3, static_cast<size_t>(n_) * bench_);
        dstBase_ = dstBase;
        dstStride_ = dstStride;
        blockBenchOffset_ = blockBenchOffset;
        actualBench_ = actualBench;
        logicalN_ = logicalN;

        const uint32_t bytes = static_cast<uint32_t>(n_ / 2 * bench_ * sizeof(int32_t));
        pipe_.InitBuffer(inSrc0_, 1, bytes);
        pipe_.InitBuffer(inSrc1_, 1, bytes);
        pipe_.InitBuffer(inSrc2_, 1, bytes);
        pipe_.InitBuffer(inSrc3_, 1, bytes);
        pipe_.InitBuffer(outDst_, 1, bytes);
        pipe_.InitBuffer(tmpBuf0_, bytes);
        pipe_.InitBuffer(tmpBuf1_, bytes);
    }

    __aicore__ inline void CopyIn()
    {
        LocalTensor<int32_t> localSrc0 = inSrc0_.AllocTensor<int32_t>();
        LocalTensor<int32_t> localSrc1 = inSrc1_.AllocTensor<int32_t>();
        LocalTensor<int32_t> localSrc2 = inSrc2_.AllocTensor<int32_t>();
        LocalTensor<int32_t> localSrc3 = inSrc3_.AllocTensor<int32_t>();

        const size_t offset = static_cast<size_t>(subCoreIdx_) * n_ * bench_ / 2;
        const int32_t count = n_ * bench_ / 2;
        DataCopy(localSrc0, gmSrc0_[offset], count);
        DataCopy(localSrc1, gmSrc1_[offset], count);
        DataCopy(localSrc2, gmSrc2_[offset], count);
        DataCopy(localSrc3, gmSrc3_[offset], count);

        inSrc0_.EnQue(localSrc0);
        inSrc1_.EnQue(localSrc1);
        inSrc2_.EnQue(localSrc2);
        inSrc3_.EnQue(localSrc3);
    }

    __aicore__ inline void Compute()
    {
        const int32_t count = n_ * bench_ / 2;
        LocalTensor<int32_t> localSrc0 = inSrc0_.DeQue<int32_t>();
        LocalTensor<int32_t> localSrc1 = inSrc1_.DeQue<int32_t>();
        LocalTensor<int32_t> localSrc2 = inSrc2_.DeQue<int32_t>();
        LocalTensor<int32_t> localSrc3 = inSrc3_.DeQue<int32_t>();
        LocalTensor<int32_t> localDst = outDst_.AllocTensor<int32_t>();
        LocalTensor<int32_t> tmp0 = tmpBuf0_.Get<int32_t>();
        LocalTensor<int32_t> tmp1 = tmpBuf1_.Get<int32_t>();

        // Horner form with full reduction after each multiply-by-128:
        // t = src3
        // t = (t * 128) mod q; t = (t + src2) mod q
        // t = (t * 128) mod q; t = (t + src1) mod q
        // t = (t * 128) mod q; t = (t + src0) mod q
        AscendC::Muls(localDst, localSrc3, 1, count);
        if (!fastFold_) {
            ReduceBarrettVec(localDst, tmp0, tmp1, count);
        }

        MulBetaAddReduceVec(localDst, localSrc2, tmp0, tmp1, count);

        MulBetaAddReduceVec(localDst, localSrc1, tmp0, tmp1, count);

        MulBetaAddReduceVec(localDst, localSrc0, tmp0, tmp1, count);

        outDst_.EnQue(localDst);
        inSrc0_.FreeTensor(localSrc0);
        inSrc1_.FreeTensor(localSrc1);
        inSrc2_.FreeTensor(localSrc2);
        inSrc3_.FreeTensor(localSrc3);
    }

    __aicore__ inline void CopyOut()
    {
        LocalTensor<int32_t> localDst = outDst_.DeQue<int32_t>();
        const int32_t halfRows = bench_ / 2;
        const int32_t rowStart = subCoreIdx_ * halfRows;
        const int32_t validRows = actualBench_ > rowStart ? MinInt(halfRows, actualBench_ - rowStart) : 0;

        if (validRows > 0 && dstStride_ == logicalN_ && logicalN_ == n_) {
            AscendC::GlobalTensor<int32_t> dstGm;
            dstGm.SetGlobalBuffer(
                dstBase_ + static_cast<size_t>(blockBenchOffset_ + rowStart) * dstStride_,
                static_cast<size_t>(validRows) * n_);
            AscendC::DataCopy(dstGm, localDst, validRows * n_);
        } else {
            for (int32_t localRow = 0; localRow < halfRows; ++localRow) {
                const int32_t row = rowStart + localRow;
                if (row >= actualBench_) {
                    break;
                }
                AscendC::GlobalTensor<int32_t> dstGm;
                dstGm.SetGlobalBuffer(dstBase_ + static_cast<size_t>(blockBenchOffset_ + row) * dstStride_, logicalN_);
                AscendC::DataCopy(dstGm, localDst[localRow * n_], logicalN_);
            }
        }
        AscendC::PipeBarrier<PIPE_ALL>();
        outDst_.FreeTensor(localDst);
    }

private:
    __aicore__ inline void ReduceBarrettVec(
        LocalTensor<int32_t>& x, LocalTensor<int32_t>& tmp0, LocalTensor<int32_t>& tmp1, int32_t count)
    {
        if (fastFold_) {
            wrap_mod_vec_runtime(x, x, q_, tmp0, tmp1, count);
            return;
        }
        barrett_mul_vec_runtime(x, q_, barrettK_, barrettMu_, tmp0, tmp1, count);
    }

    __aicore__ inline void MulBetaModVec(
        LocalTensor<int32_t>& x, LocalTensor<int32_t>& tmp0, LocalTensor<int32_t>& tmp1, int32_t count)
    {
        MulBaseModVecRuntime(x, q_, fold28_, tmp0, tmp1, count);
    }

    __aicore__ inline void MulBetaAddReduceVec(
        LocalTensor<int32_t>& x, LocalTensor<int32_t>& addend, LocalTensor<int32_t>& tmp0, LocalTensor<int32_t>& tmp1,
        int32_t count)
    {
        if (fastFold_) {
            MulBase7FoldNoWrapVec(x, fold28_, tmp0, tmp1, count);
            AscendC::Add(x, x, addend, count);
            wrap_mod_vec_runtime(x, x, q_, tmp0, tmp1, count);
            return;
        }
        MulBetaModVec(x, tmp0, tmp1, count);
        AscendC::Add(x, x, addend, count);
        ReduceBarrettVec(x, tmp0, tmp1, count);
    }

private:
    const int32_t subCoreIdx_;
    const int32_t bench_;
    const uint32_t n_;
    const int32_t q_;
    const int32_t barrettK_;
    const int32_t barrettMu_;
    const int32_t fold28_;
    const bool fastFold_;
    __gm__ int32_t* dstBase_ = nullptr;
    int32_t dstStride_ = 0;
    int32_t blockBenchOffset_ = 0;
    int32_t actualBench_ = 0;
    int32_t logicalN_ = 0;

    AscendC::TPipe pipe_;
    AscendC::TBuf<AscendC::TPosition::VECCALC> tmpBuf0_, tmpBuf1_;
    AscendC::TQue<AscendC::TPosition::VECIN, 1> inSrc0_, inSrc1_, inSrc2_, inSrc3_;
    AscendC::TQue<AscendC::TPosition::VECOUT, 1> outDst_;

    AscendC::GlobalTensor<int32_t> gmSrc0_, gmSrc1_, gmSrc2_, gmSrc3_;
};

