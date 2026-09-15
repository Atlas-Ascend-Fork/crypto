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

#include "kernel_operator.h"
#include <cstdint>

template <int Count>
class NttVectorBuffers {
public:
    __aicore__ inline void Init(int32_t length, int32_t wideIndex = -1)
    {
        for (int32_t i = 0; i < Count; ++i) {
            const uint32_t bytes = static_cast<uint32_t>(length * sizeof(int32_t) * (i == wideIndex ? 2 : 1));
            pipe_.InitBuffer(storage_[i], bytes);
        }
    }

    template <typename T>
    __aicore__ inline AscendC::LocalTensor<T> Get(int32_t index)
    {
        return storage_[index].template Get<T>();
    }

private:
    AscendC::TPipe pipe_;
    AscendC::TBuf<AscendC::TPosition::VECCALC> storage_[Count];
};

struct NttSplitBuffers {
    AscendC::TPipe pipe;
    AscendC::TQue<AscendC::TPosition::VECIN, 1> input;
    AscendC::TQue<AscendC::TPosition::VECOUT, 1> output[4];
    AscendC::TBuf<AscendC::TPosition::VECCALC> tmp0, tmp1, tmp2;

    __aicore__ inline void Init(int32_t length, bool extraTemp)
    {
        pipe.InitBuffer(input, 1, length * sizeof(int32_t));
        for (int32_t i = 0; i < 4; ++i) {
            pipe.InitBuffer(output[i], 1, length * sizeof(int8_t));
        }
        pipe.InitBuffer(tmp0, length * sizeof(int32_t));
        pipe.InitBuffer(tmp1, length * sizeof(half));
        if (extraTemp) {
            pipe.InitBuffer(tmp2, length * sizeof(int32_t));
        }
    }
};

struct KemParitySplitBuffers {
    AscendC::TPipe pipe;
    AscendC::TQue<AscendC::TPosition::VECIN, 1> input;
    AscendC::TBuf<AscendC::TPosition::VECCALC> digit0, digit1, tmp0, tmp1;

    __aicore__ inline void Init(int32_t length)
    {
        pipe.InitBuffer(input, 1, length * sizeof(int32_t));
        pipe.InitBuffer(digit0, length * sizeof(int8_t));
        pipe.InitBuffer(digit1, length * sizeof(int8_t));
        pipe.InitBuffer(tmp0, length * sizeof(int32_t));
        pipe.InitBuffer(tmp1, length * sizeof(half));
    }
};

struct PqcPanel {
    int32_t n = 0;
    int32_t tileCount = 0;
    int32_t tileId = 0;
    int32_t panelWidth = 0;
    int32_t coeffStart = 0;
    int32_t polyStart = 0;
    int32_t validRows = 0;
    int32_t tileLength = 0;
    bool valid = false;
};

struct NttScratch {
    GM_ADDR base;
    size_t splitBytes;
    size_t accumBytes;

    __aicore__ inline GM_ADDR Split(int32_t digit) const
    {
        return base + static_cast<size_t>(digit) * splitBytes;
    }

    __aicore__ inline GM_ADDR Accum(int32_t digit) const
    {
        return base + 4 * splitBytes + static_cast<size_t>(digit) * accumBytes;
    }
};

struct DirectSplitPlan {
    int32_t srcStride;
    int32_t blockBenchOffset;
    int32_t logicalN;
    int32_t kernelN;
    int32_t paddedBenchWidth;
    int32_t halfRows;
    int32_t rowStart;
    int32_t tileLength;
    int32_t validRows;
    bool denseBenchLayout;
};

struct Phase2Buffers {
    NttVectorBuffers<18> storage;
    AscendC::LocalTensor<int32_t> input, twiddle, product, output, temp[13];
    AscendC::LocalTensor<uint32_t> transpose;

    __aicore__ inline void Init(int32_t count)
    {
        storage.Init(count);
        input = storage.Get<int32_t>(0);
        twiddle = storage.Get<int32_t>(1);
        product = storage.Get<int32_t>(2);
        output = storage.Get<int32_t>(3);
        transpose = storage.Get<uint32_t>(4);
        for (int32_t i = 0; i < 13; ++i) {
            temp[i] = storage.Get<int32_t>(5 + i);
        }
    }
};

struct RowPairBuffers {
    NttVectorBuffers<21> storage;
    AscendC::LocalTensor<int32_t> input, fullInput, twiddle, product, output, temp[13];
    AscendC::LocalTensor<uint32_t> transpose, select[2];

    __aicore__ inline void Init(int32_t count)
    {
        storage.Init(count, 1);
        input = storage.Get<int32_t>(0);
        fullInput = storage.Get<int32_t>(1);
        twiddle = storage.Get<int32_t>(2);
        product = storage.Get<int32_t>(3);
        output = storage.Get<int32_t>(4);
        transpose = storage.Get<uint32_t>(5);
        select[0] = storage.Get<uint32_t>(6);
        select[1] = storage.Get<uint32_t>(7);
        for (int32_t i = 0; i < 13; ++i) {
            temp[i] = storage.Get<int32_t>(8 + i);
        }
    }
};

struct PanelWork {
    int32_t colPanelCount = 0;
    int32_t panelsPerPoly = 0;
    int32_t panelStart = 0;
    int32_t panelEnd = 0;
    bool active = false;
};

struct PanelRect {
    int32_t rowStart;
    int32_t colStart;
    int32_t rows;
    int32_t cols;
};

struct RowPairWork {
    int32_t rowStart = 0;
    int32_t polyStart = 0;
    int32_t polyEnd = 0;
    bool active = false;
};
