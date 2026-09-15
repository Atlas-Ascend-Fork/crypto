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

#include "ntt_device.hpp"

__aicore__ inline size_t CubeNtt::ParallelBatSplitPlaneBytes() const
{
    return static_cast<size_t>(ParallelBatPaddedPolyCount()) *
           static_cast<size_t>(tiling_.n) * sizeof(int8_t);
}

__aicore__ inline GM_ADDR CubeNtt::ParallelBatSplitPlane(int32_t digit) const
{
    return scratchBase_ +
           static_cast<size_t>(digit) * ParallelBatSplitPlaneBytes();
}

__aicore__ inline void CubeNtt::StoreKemParityDigits(
    LocalTensor<int8_t>& digit0, LocalTensor<int8_t>& digit1, int32_t polyStart)
{
    constexpr int32_t n = 256;
    constexpr int32_t halfN = n / 2;
    constexpr int32_t tileRows = kParallelAivPolyBatch;
    const size_t tileOffset = static_cast<size_t>(polyStart) * n;
    AscendC::DataCopyParams copyOut{
        static_cast<uint16_t>(tileRows),
        static_cast<uint16_t>(halfN * sizeof(int8_t) / 32), 0,
        static_cast<uint16_t>(halfN * sizeof(int8_t) / 32)};
    AscendC::GlobalTensor<int8_t> evenPlane;
    evenPlane.SetGlobalBuffer(
        reinterpret_cast<__gm__ int8_t*>(ParallelBatSplitPlane(subBlockId_)) + tileOffset,
        tileRows * n);
    AscendC::DataCopy(evenPlane, digit0, copyOut);
    AscendC::DataCopy(evenPlane[halfN], digit1, copyOut);
    AscendC::PipeBarrier<PIPE_ALL>();
}

__aicore__ inline void CubeNtt::ParallelKemParitySplit(
    __gm__ int32_t* srcBase, int32_t tileId)
{
    constexpr int32_t tileRows = kParallelAivPolyBatch;
    constexpr int32_t n = 256;
    constexpr int32_t halfN = n / 2;
    constexpr int32_t parityLength = tileRows * halfN;
    const int32_t polyStart = tileId * tileRows;
    const int32_t validRows = polyStart < tiling_.polyCount
        ? MinInt(tileRows, tiling_.polyCount - polyStart)
        : 0;
    if (validRows <= 0) {
        return;
    }

    KemParitySplitBuffers buffers;
    buffers.Init(parityLength);

    LocalTensor<int8_t> digit0 = buffers.digit0.Get<int8_t>();
    LocalTensor<int8_t> digit1 = buffers.digit1.Get<int8_t>();
    LocalTensor<int32_t> tmp0 = buffers.tmp0.Get<int32_t>();
    LocalTensor<half> tmp1 = buffers.tmp1.Get<half>();

    LocalTensor<int32_t> evenInput = buffers.input.AllocTensor<int32_t>();
    if (validRows < tileRows) {
        AscendC::Duplicate(
            evenInput, static_cast<int32_t>(0), parityLength);
    }
    AscendC::GlobalTensor<int32_t> evenSrcGm;
    evenSrcGm.SetGlobalBuffer(
        srcBase + static_cast<size_t>(subBlockId_) * tiling_.polyCount * halfN +
            static_cast<size_t>(polyStart) * halfN,
        static_cast<size_t>(validRows) * halfN);
    AscendC::DataCopy(evenInput, evenSrcGm, validRows * halfN);
    buffers.input.EnQue(evenInput);
    LocalTensor<int32_t> even = buffers.input.DeQue<int32_t>();
    Tensor_int8x2 evenSplit{digit0, digit1};
    split_vec_int7x2(
        evenSplit, even, tmp0, tmp1, parityLength);
    AscendC::PipeBarrier<PIPE_ALL>();
    StoreKemParityDigits(digit0, digit1, polyStart);
    buffers.input.FreeTensor(even);
}

__aicore__ inline void CubeNtt::ParallelKemParityMmad(
    GM_ADDR matrixBase, int32_t tileId)
{
    constexpr int32_t n = 256;
    constexpr int32_t tileRows = kParallelAivPolyBatch;
    const int32_t tileCount = CeilDivInt(
        ParallelBatPaddedPolyCount(), tileRows);
    if (tileId >= tileCount) {
        return;
    }

    const size_t matrixBytes = static_cast<size_t>(n) * n;
    AicMmad mm(tileRows, n, 128);
    mm.Init();
    const size_t inputTileOffset =
        static_cast<size_t>(tileId) * tileRows * n;
    const size_t outputTileOffset =
        static_cast<size_t>(tileId) * tileRows * 128;
    for (int32_t matrixId = 0; matrixId < 4; ++matrixId) {
        const int32_t parity = matrixId / 2;
        mm.LoadA(
            ParallelBatSplitPlane(parity) + inputTileOffset);
        mm.LoadB(
            matrixBase + static_cast<size_t>(matrixId) * matrixBytes);
        LocalTensor<int32_t> partial = mm.AllocAccum();
        mm.ComputeAccum(partial, true);
        mm.EnqueAccum(partial);
        mm.FreeA();
        mm.FreeB();
        mm.CopyOutQueued(
            ParallelBatPartialMatrix(matrixId) +
            outputTileOffset * sizeof(int32_t));
    }
}

__aicore__ inline void CubeNtt::ParallelDsaTileMmad(
    GM_ADDR matrixBase, int32_t tileId)
{
    constexpr int32_t n = 256;
    constexpr int32_t tileRows = kParallelAivPolyBatch;
    constexpr int32_t matrixCount = 9;
    const size_t matrixBytes = static_cast<size_t>(n) * n;
    const size_t tileOffset =
        static_cast<size_t>(tileId) * tileRows * n;
    AicMmad mm(tileRows, n, n);
    mm.Init();
    for (int32_t matrixId = 0; matrixId < matrixCount; ++matrixId) {
        mm.LoadA(
            ParallelBatSplitPlane(matrixId % 3) + tileOffset);
        mm.LoadB(
            matrixBase + static_cast<size_t>(matrixId) * matrixBytes);
        LocalTensor<int32_t> partial = mm.AllocAccum();
        mm.ComputeAccum(partial, true);
        mm.EnqueAccum(partial);
        mm.FreeA();
        mm.FreeB();
        mm.CopyOutQueued(
            ParallelBatPartialMatrix(matrixId) +
            tileOffset * sizeof(int32_t));
    }
}

__aicore__ inline PqcPanel CubeNtt::MakePqcPanel(int32_t forcedTile) const
{
    PqcPanel panel;
    panel.n = tiling_.n;
    panel.tileCount = CeilDivInt(ParallelBatPaddedPolyCount(), kParallelAivPolyBatch);
    if (panel.tileCount <= 0) {
        return panel;
    }
    const int32_t workerId = logicalBlockId_ * kMixAivPerAic + subBlockId_;
    const int32_t workerCount = logicalBlockCount_ * kMixAivPerAic;
    const int32_t panelsPerTile = forcedTile >= 0 ? kMixAivPerAic : MinInt(8, workerCount / panel.tileCount);
    if (panelsPerTile <= 0) {
        return panel;
    }
    panel.tileId = forcedTile >= 0 ? forcedTile : workerId / panelsPerTile;
    if (panel.tileId >= panel.tileCount) {
        return panel;
    }
    const int32_t panelId = forcedTile >= 0 ? subBlockId_ : workerId - panel.tileId * panelsPerTile;
    panel.panelWidth = panel.n / panelsPerTile;
    panel.coeffStart = panelId * panel.panelWidth;
    panel.polyStart = panel.tileId * kParallelAivPolyBatch;
    panel.validRows = panel.polyStart < tiling_.polyCount ?
        MinInt(kParallelAivPolyBatch, tiling_.polyCount - panel.polyStart) : 0;
    panel.tileLength = panel.panelWidth * kParallelAivPolyBatch;
    panel.valid = true;
    return panel;
}

__aicore__ inline void CubeNtt::LoadPqcInput(
    NttSplitBuffers& buffers, __gm__ int32_t* srcBase, const PqcPanel& panel)
{
    LocalTensor<int32_t> localSrc = buffers.input.AllocTensor<int32_t>();
    if (panel.validRows < kParallelAivPolyBatch) {
        AscendC::Duplicate(
            localSrc, static_cast<int32_t>(0), panel.tileLength);
    }
    if (panel.validRows > 0) {
        AscendC::GlobalTensor<int32_t> srcGm;
        srcGm.SetGlobalBuffer(
            srcBase + static_cast<size_t>(panel.polyStart) * panel.n + panel.coeffStart,
            static_cast<size_t>(panel.validRows) * panel.n);
        AscendC::DataCopyParams copyIn{
            static_cast<uint16_t>(panel.validRows),
            static_cast<uint16_t>(panel.panelWidth * sizeof(int32_t) / 32),
            static_cast<uint16_t>((panel.n - panel.panelWidth) * sizeof(int32_t) / 32),
            0};
        AscendC::DataCopy(localSrc, srcGm, copyIn);
    }
    buffers.input.EnQue(localSrc);

}

__aicore__ inline void CubeNtt::ComputePqcSplit(NttSplitBuffers& buffers, int32_t tileLength)
{
    LocalTensor<int32_t> src = buffers.input.DeQue<int32_t>();
    LocalTensor<int8_t> digit0 = buffers.output[0].AllocTensor<int8_t>();
    LocalTensor<int8_t> digit1 = buffers.output[1].AllocTensor<int8_t>();
    LocalTensor<int32_t> t0 = buffers.tmp0.Get<int32_t>();
    LocalTensor<half> t1 = buffers.tmp1.Get<half>();
    LocalTensor<int32_t> t2 = buffers.tmp2.Get<int32_t>();
    if (activeQ_ == 3329) {
        Tensor_int8x2 digits{digit0, digit1};
        split_vec_int7x2(digits, src, t0, t1, tileLength);
    } else {
        LocalTensor<int8_t> digit2 = buffers.output[2].AllocTensor<int8_t>();
        Tensor_int8x3 digits{digit0, digit1, digit2};
        split_vec_int8x3_balanced(
            digits, src, t0, t2, t1, tileLength);
        buffers.output[2].EnQue(digit2);
    }
    buffers.output[0].EnQue(digit0);
    buffers.output[1].EnQue(digit1);
    buffers.input.FreeTensor(src);

}

__aicore__ inline void CubeNtt::StorePqcSplit(NttSplitBuffers& buffers, const PqcPanel& panel)
{
    LocalTensor<int8_t> result0 = buffers.output[0].DeQue<int8_t>();
    LocalTensor<int8_t> result1 = buffers.output[1].DeQue<int8_t>();
    const size_t polyOffset = static_cast<size_t>(panel.polyStart) * panel.n;
    AscendC::GlobalTensor<int8_t> dst0, dst1;
    dst0.SetGlobalBuffer(
        reinterpret_cast<__gm__ int8_t*>(ParallelBatSplitPlane(0)) +
            polyOffset + panel.coeffStart,
        static_cast<size_t>(kParallelAivPolyBatch) * panel.n);
    dst1.SetGlobalBuffer(
        reinterpret_cast<__gm__ int8_t*>(ParallelBatSplitPlane(1)) +
            polyOffset + panel.coeffStart,
        static_cast<size_t>(kParallelAivPolyBatch) * panel.n);
    AscendC::DataCopyParams copyOut{
        static_cast<uint16_t>(kParallelAivPolyBatch),
        static_cast<uint16_t>(panel.panelWidth * sizeof(int8_t) / 32), 0,
        static_cast<uint16_t>((panel.n - panel.panelWidth) * sizeof(int8_t) / 32)};
    AscendC::DataCopy(dst0, result0, copyOut);
    AscendC::DataCopy(dst1, result1, copyOut);
    if (activeQ_ != 3329) {
        LocalTensor<int8_t> result2 = buffers.output[2].DeQue<int8_t>();
        AscendC::GlobalTensor<int8_t> dst2;
        dst2.SetGlobalBuffer(
            reinterpret_cast<__gm__ int8_t*>(ParallelBatSplitPlane(2)) +
                polyOffset + panel.coeffStart,
            static_cast<size_t>(kParallelAivPolyBatch) * panel.n);
        AscendC::DataCopy(dst2, result2, copyOut);
        buffers.output[2].FreeTensor(result2);
    }
    AscendC::PipeBarrier<PIPE_ALL>();
    buffers.output[0].FreeTensor(result0);
    buffers.output[1].FreeTensor(result1);
}

__aicore__ inline void CubeNtt::ParallelBatSplit(__gm__ int32_t* srcBase, int32_t forcedTile)
{
    const auto panel = MakePqcPanel(forcedTile);
    if (!panel.valid) {
        return;
    }
    NttSplitBuffers buffers;
    buffers.Init(panel.tileLength, true);
    LoadPqcInput(buffers, srcBase, panel);
    ComputePqcSplit(buffers, panel.tileLength);
    StorePqcSplit(buffers, panel);
}

__aicore__ inline void CubeNtt::MmadPqcTile(AicMmad& mm, int32_t matrixId, int32_t inDigit, int32_t tile)
{
    const size_t offset = static_cast<size_t>(tile) * tiling_.subBench * tiling_.n;
    mm.LoadA(ParallelBatSplitPlane(inDigit) + offset);
    LocalTensor<int32_t> partial = mm.AllocAccum();
    mm.ComputeAccum(partial, true);
    mm.EnqueAccum(partial);
    mm.FreeA();
    mm.CopyOutQueued(ParallelBatPartialMatrix(matrixId) + offset * sizeof(int32_t));
}

__aicore__ inline void CubeNtt::ParallelKemMmadTiles(AicMmad& mm, GM_ADDR matrixBase, int32_t tileCount)
{
    const int32_t owner = logicalBlockId_ % 4;
    const int32_t matrixId = owner < 2 ? owner : owner + 2;
    const size_t matrixBytes = static_cast<size_t>(tiling_.n) * tiling_.n;
    mm.LoadB(matrixBase + static_cast<size_t>(matrixId) * matrixBytes);
    for (int32_t tile = logicalBlockId_ / 4; tile < tileCount; tile += 4) {
        MmadPqcTile(mm, matrixId, matrixId % kDigitCount, tile);
    }
    mm.FreeB();
}

__aicore__ inline void CubeNtt::ParallelDsaMmadTiles(AicMmad& mm, GM_ADDR matrixBase, int32_t tileCount)
{
    if (tileCount == 0) {
        return;
    }
    if (logicalBlockCount_ == 0) {
        return;
    }
    if (tileCount < 0 || logicalBlockCount_ < 0) {
        return;
    }
    const size_t matrixBytes = static_cast<size_t>(tiling_.n) * tiling_.n;
    const int32_t totalTasks = 9 * tileCount;
    int32_t task = totalTasks * logicalBlockId_ / logicalBlockCount_;
    const int32_t taskEnd = totalTasks * (logicalBlockId_ + 1) / logicalBlockCount_;
    while (task < taskEnd) {
        const int32_t matrixId = task / tileCount;
        const int32_t matrixEnd = MinInt(taskEnd, (matrixId + 1) * tileCount);
        mm.LoadB(matrixBase + static_cast<size_t>(matrixId) * matrixBytes);
        for (; task < matrixEnd; ++task) {
            MmadPqcTile(mm, matrixId, matrixId % 3, task - matrixId * tileCount);
        }
        mm.FreeB();
    }
}

__aicore__ inline void CubeNtt::ParallelBatMmad(GM_ADDR matrixBase)
{
    const int32_t tileCount = CeilDivInt(ParallelBatPaddedPolyCount(), tiling_.subBench);
    AicMmad mm(tiling_.subBench, tiling_.n, tiling_.n);
    mm.Init();
    if (activeQ_ == 3329) {
        ParallelKemMmadTiles(mm, matrixBase, tileCount);
    } else {
        ParallelDsaMmadTiles(mm, matrixBase, tileCount);
    }
}
