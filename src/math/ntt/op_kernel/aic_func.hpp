/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef __AIC_FUNC_HPP__
#define __AIC_FUNC_HPP__
#include "kernel_operator.h"
#include "ntt_tiling_data.h"
#include "ntt_macros.h"

template <typename T, typename U>
__aicore__ inline static constexpr T max(T a, U b)
{
    return (a > (T)b) ? a : (T)b;
}

// int8_t type, cube block: [16, 32]
static constexpr uint32_t CUBE_BLOCK_SIZE = 16 * 32;
constexpr size_t matMFileSize = 256 * 256 * sizeof(int32_t);
static constexpr __aicore__ inline uint16_t ceil_div(uint16_t a, uint16_t mod)
{
    return (a + mod - 1) / mod;
}

class AicMmad {
public:
    __aicore__ inline AicMmad(uint16_t m, uint16_t k, uint16_t n) : m(m), k(k), n(n)
    {
        aSize = max(16, m) * k;
        bSize = k * n;
        cSize = max(16, m) * n;
    }

    __aicore__ inline void Init()
    {
        pipe.InitBuffer(inQueueA1, 1, aSize * sizeof(int8_t));
        pipe.InitBuffer(inQueueA2, 1, aSize * sizeof(int8_t));
        pipe.InitBuffer(inQueueB1, 1, bSize * sizeof(int8_t));
        pipe.InitBuffer(inQueueB2, 1, bSize * sizeof(int8_t));
        pipe.InitBuffer(outQueueCO1, 4, cSize * sizeof(int32_t));
        aLoaded = false;
        bLoaded = false;
    }

    __aicore__ inline void LoadA(GM_ADDR a)
    {
        aGM.SetGlobalBuffer((__gm__ int8_t*)a);
        CopyInA();
        SplitA();
        a2Local = inQueueA2.DeQue<int8_t>();
        aLoaded = true;
    }

    __aicore__ inline void LoadB(GM_ADDR b)
    {
        bGM.SetGlobalBuffer((__gm__ int8_t*)b);
        CopyInBPrepacked();
        SplitB();
        b2Local = inQueueB2.DeQue<int8_t>();
        bLoaded = true;
    }

    __aicore__ inline void FreeA()
    {
        if (aLoaded) {
            inQueueA2.FreeTensor(a2Local);
            aLoaded = false;
        }
    }

    __aicore__ inline void FreeB()
    {
        if (bLoaded) {
            inQueueB2.FreeTensor(b2Local);
            bLoaded = false;
        }
    }

    __aicore__ inline AscendC::LocalTensor<int32_t> AllocAccum()
    {
        return outQueueCO1.AllocTensor<int32_t>();
    }

    __aicore__ inline void EnqueAccum(AscendC::LocalTensor<int32_t>& cLocal)
    {
        outQueueCO1.EnQue<int32_t>(cLocal);
    }

    __aicore__ inline void ComputeAccum(AscendC::LocalTensor<int32_t>& cLocal, bool initC)
    {
        AscendC::MmadParams mmadParams;
        mmadParams.m = ceil_div(m, 16) * 16;
        mmadParams.k = k;
        mmadParams.n = n;
        mmadParams.cmatrixInitVal = initC;
        AscendC::Mmad(cLocal, a2Local, b2Local, mmadParams);
    }

    __aicore__ inline void CopyOutQueued(GM_ADDR dst)
    {
        cGM.SetGlobalBuffer((__gm__ int32_t*)dst);
        AscendC::LocalTensor<int32_t> cLocal = outQueueCO1.DeQue<int32_t>();
        AscendC::FixpipeParamsV220 fixpipeParams;
        fixpipeParams.nSize = n;
        fixpipeParams.mSize = m;
        fixpipeParams.srcStride = ceil_div(m, 16) * 16;
        fixpipeParams.dstStride = n;
        fixpipeParams.ndNum = 1;
        fixpipeParams.srcNdStride = 0;
        fixpipeParams.dstNdStride = 0;
        AscendC::Fixpipe(cGM, cLocal, fixpipeParams);
        outQueueCO1.FreeTensor(cLocal);
    }

private:
    __aicore__ inline void CopyInA()
    {
        AscendC::LocalTensor<int8_t> a1Local = inQueueA1.AllocTensor<int8_t>();
        AscendC::Nd2NzParams nd2nzA1Params;
        nd2nzA1Params.ndNum = 1;
        nd2nzA1Params.nValue = m;
        nd2nzA1Params.dValue = k;
        nd2nzA1Params.srcNdMatrixStride = 0;
        nd2nzA1Params.srcDValue = k;
        nd2nzA1Params.dstNzC0Stride = ceil_div(m, 16) * 16;
        nd2nzA1Params.dstNzNStride = 1;
        nd2nzA1Params.dstNzMatrixStride = 0;
        AscendC::DataCopy(a1Local, aGM, nd2nzA1Params);
        inQueueA1.EnQue(a1Local);
    }

    __aicore__ inline void CopyInBPrepacked()
    {
        AscendC::LocalTensor<int8_t> b1Local = inQueueB1.AllocTensor<int8_t>();
        AscendC::DataCopy(b1Local, bGM, static_cast<uint32_t>(bSize));
        inQueueB1.EnQue(b1Local);
    }

    __aicore__ inline void SplitA()
    {
        LocalTensor<int8_t> a1Local = inQueueA1.DeQue<int8_t>();
        LocalTensor<int8_t> a2Local = inQueueA2.AllocTensor<int8_t>();

        uint32_t dstOffset = ceil_div(k, 32) * CUBE_BLOCK_SIZE;
        uint32_t srcOffset = CUBE_BLOCK_SIZE;

#ifdef ASCENDC_CPU_DEBUG
        for (int i = 0; i < max(16, m) * k; i++) {
            a2Local.SetValue(i, -1);
        }
#endif

        AscendC::LoadData2dParams loadDataParams;
        loadDataParams.repeatTimes = ceil_div(k, 32);
        loadDataParams.srcStride = ceil_div(m, 16);
        loadDataParams.dstGap = 0;
        loadDataParams.ifTranspose = false;
        for (int32_t i = 0; i < ceil_div(m, 16); i++) {
            AscendC::LoadData(a2Local[i * dstOffset], a1Local[i * srcOffset], loadDataParams);
        }
        inQueueA1.FreeTensor(a1Local);
        inQueueA2.EnQue<int8_t>(a2Local);
    }

    __aicore__ inline void SplitB()
    {
        LocalTensor<int8_t> b1Local = inQueueB1.DeQue<int8_t>();
        LocalTensor<int8_t> b2Local = inQueueB2.AllocTensor<int8_t>();

        uint32_t dstOffset = ceil_div(n, 32) * (2 * CUBE_BLOCK_SIZE);
        uint32_t srcOffset = 2 * CUBE_BLOCK_SIZE;

        AscendC::LoadData2dTransposeParams loadDataParams;
        loadDataParams.repeatTimes = ceil_div(n, 32);
        loadDataParams.srcStride = ceil_div(k, 32);
        loadDataParams.dstGap = 1;
        loadDataParams.dstFracGap = 0;

#ifdef ASCENDC_CPU_DEBUG
        for (int i = 0; i < k * n; i++) {
            b2Local.SetValue(i, -1);
        }
#endif

        for (int32_t i = 0; i < ceil_div(k, 32); i++) {
            AscendC::LoadDataWithTranspose(b2Local[i * dstOffset], b1Local[i * srcOffset], loadDataParams);
        }
        inQueueB1.FreeTensor(b1Local);
        inQueueB2.EnQue<int8_t>(b2Local);
    }

private:
    AscendC::TPipe pipe;
    AscendC::TQue<AscendC::TPosition::A1, 1> inQueueA1;
    AscendC::TQue<AscendC::TPosition::A2, 1> inQueueA2;
    AscendC::TQue<AscendC::TPosition::B1, 1> inQueueB1;
    AscendC::TQue<AscendC::TPosition::B2, 1> inQueueB2;
    AscendC::TQue<AscendC::TPosition::CO1, 4> outQueueCO1;
    AscendC::LocalTensor<int8_t> a2Local, b2Local;

    AscendC::GlobalTensor<int8_t> aGM;
    AscendC::GlobalTensor<int8_t> bGM;
    AscendC::GlobalTensor<int32_t> cGM;
    uint16_t m, k, n;
    size_t aSize, bSize, cSize;
    bool aLoaded = false;
    bool bLoaded = false;
};

#endif
