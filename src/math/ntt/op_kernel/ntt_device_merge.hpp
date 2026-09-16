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

__aicore__ inline void CubeNtt::ParallelKemParityMerge(__gm__ int32_t* dstBase, int32_t tileId)
{
    constexpr int32_t n = 256;
    constexpr int32_t halfN = n / 2;
    constexpr int32_t tileRows = kParallelAivPolyBatch;
    constexpr int32_t halfLength = tileRows * halfN;
    const int32_t polyStart = tileId * tileRows;
    const int32_t validRows = polyStart < tiling_.polyCount ? MinInt(tileRows, tiling_.polyCount - polyStart) : 0;
    if (validRows <= 0) {
        return;
    }

    NttVectorBuffers<5> buffers;
    buffers.Init(halfLength);
    LocalTensor<int32_t> digit0 = buffers.Get<int32_t>(0);
    LocalTensor<int32_t> digit1 = buffers.Get<int32_t>(1);
    LocalTensor<int32_t> result = buffers.Get<int32_t>(2);
    LocalTensor<int32_t> tmp0 = buffers.Get<int32_t>(3);
    LocalTensor<int32_t> tmp1 = buffers.Get<int32_t>(4);

    const size_t tileOffset = static_cast<size_t>(polyStart) * halfN;
    const int32_t matrixId = 2 * subBlockId_;
    AscendC::GlobalTensor<int32_t> partialGm;
    partialGm.SetGlobalBuffer(
        reinterpret_cast<__gm__ int32_t*>(ParallelBatPartialMatrix(matrixId)) + tileOffset, halfLength);
    AscendC::DataCopy(digit0, partialGm, halfLength);
    partialGm.SetGlobalBuffer(
        reinterpret_cast<__gm__ int32_t*>(ParallelBatPartialMatrix(matrixId + 1)) + tileOffset, halfLength);
    AscendC::DataCopy(digit1, partialGm, halfLength);
    AscendC::PipeBarrier<PIPE_ALL>();
    AscendC::Muls(result, digit1, 1, halfLength);
    ParallelBatReduce(result, tmp0, tmp1, halfLength, false);
    ParallelBatMergeStep(result, digit0, tmp0, tmp1, halfLength, false);
    AscendC::PipeBarrier<PIPE_ALL>();

    AscendC::GlobalTensor<int32_t> dst;
    dst.SetGlobalBuffer(
        dstBase + static_cast<size_t>(subBlockId_) * tiling_.polyCount * halfN + static_cast<size_t>(polyStart) * halfN,
        static_cast<size_t>(validRows) * halfN);
    AscendC::DataCopy(dst, result, validRows * halfN);
    AscendC::PipeBarrier<PIPE_ALL>();
}

__aicore__ inline void CubeNtt::ParallelBatLoadDigitSum(
    LocalTensor<int32_t>& dst, LocalTensor<int32_t>& tmp, int32_t outDigit, int32_t polyStart, int32_t coeffStart,
    int32_t panelWidth, int32_t n)
{
    const size_t polyOffset = static_cast<size_t>(polyStart) * n;
    const int32_t count = kParallelAivPolyBatch * panelWidth;
    AscendC::DataCopyParams copyIn{
        static_cast<uint16_t>(kParallelAivPolyBatch), static_cast<uint16_t>(panelWidth * sizeof(int32_t) / 32),
        static_cast<uint16_t>((n - panelWidth) * sizeof(int32_t) / 32), 0};
    const int32_t matrixDigitCount = activeQ_ == 3329 ? 4 : 3;
    AscendC::GlobalTensor<int32_t> src;
    src.SetGlobalBuffer(
        reinterpret_cast<__gm__ int32_t*>(ParallelBatPartialMatrix(outDigit * matrixDigitCount)) + polyOffset +
            coeffStart,
        static_cast<size_t>(kParallelAivPolyBatch) * n);
    AscendC::DataCopy(dst, src, copyIn);
    AscendC::PipeBarrier<PIPE_ALL>();

    const int32_t inputDigitCount = activeQ_ == 3329 ? 2 : 3;
    for (int32_t inDigit = 1; inDigit < inputDigitCount; ++inDigit) {
        src.SetGlobalBuffer(
            reinterpret_cast<__gm__ int32_t*>(ParallelBatPartialMatrix(outDigit * matrixDigitCount + inDigit)) +
                polyOffset + coeffStart,
            static_cast<size_t>(kParallelAivPolyBatch) * n);
        AscendC::DataCopy(tmp, src, copyIn);
        AscendC::PipeBarrier<PIPE_ALL>();
        AscendC::Add(dst, dst, tmp, count);
        AscendC::PipeBarrier<PIPE_ALL>();
    }
}

__aicore__ inline void CubeNtt::ParallelBatReduce(
    LocalTensor<int32_t>& value, LocalTensor<int32_t>& tmp0, LocalTensor<int32_t>& tmp1, int32_t count, bool fastFold)
{
    if (fastFold) {
        wrap_mod_vec_runtime(value, value, activeQ_, tmp0, tmp1, count);
        return;
    }
    barrett_mul_vec_runtime(
        value, activeQ_, activeBarrettK_, static_cast<int32_t>(activeBarrettMu_), tmp0, tmp1, count);
}

__aicore__ inline void CubeNtt::ParallelBatReduceSignedDsa(
    LocalTensor<int32_t>& value, LocalTensor<int32_t>& tmp0, LocalTensor<int32_t>& tmp1, int32_t count)
{
    // A 3x256xint8 dot product is in [-12.5M, 12.6M].  Biasing by
    // 2*q makes the existing Barrett reduction valid for signed sums.
    AscendC::Adds(value, value, 2 * 8380417, count);
    barrett_mul_vec_runtime(value, 8380417, activeBarrettK_, static_cast<int32_t>(activeBarrettMu_), tmp0, tmp1, count);
}

__aicore__ inline void CubeNtt::ParallelBatMulBase256Dsa(
    LocalTensor<int32_t>& value, LocalTensor<int32_t>& tmp0, LocalTensor<int32_t>& tmp1, int32_t count)
{
    // q = 2^23 - 8191.  Fold value*256 at bit 23 before it reaches
    // the signed-int32 overflow boundary.
    ShiftRightU(tmp0, value, 15, count);
    AscendC::ShiftLeft(tmp1, tmp0, (int32_t)23, count);
    AscendC::ShiftLeft(value, value, (int32_t)8, count);
    AscendC::Sub(value, value, tmp1, count);
    AscendC::Muls(tmp0, tmp0, 8191, count);
    AscendC::Add(value, value, tmp0, count);
    wrap_mod_vec_runtime(value, value, 8380417, tmp0, tmp1, count);
}

__aicore__ inline void CubeNtt::ParallelBatMergeStepDsa(
    LocalTensor<int32_t>& value, LocalTensor<int32_t>& addend, LocalTensor<int32_t>& tmp0, LocalTensor<int32_t>& tmp1,
    int32_t count)
{
    ParallelBatMulBase256Dsa(value, tmp0, tmp1, count);
    ParallelBatReduceSignedDsa(addend, tmp0, tmp1, count);
    AscendC::Add(value, value, addend, count);
    wrap_mod_vec_runtime(value, value, 8380417, tmp0, tmp1, count);
}

__aicore__ inline void CubeNtt::ParallelBatMergeStep(
    LocalTensor<int32_t>& value, LocalTensor<int32_t>& addend, LocalTensor<int32_t>& tmp0, LocalTensor<int32_t>& tmp1,
    int32_t count, bool fastFold)
{
    if (activeQ_ == 3329 || activeQ_ == 8380417) {
        AscendC::Muls(value, value, kDigitBase, count);
        AscendC::Add(value, value, addend, count);
        ParallelBatReduce(value, tmp0, tmp1, count, false);
        return;
    }
    if (fastFold) {
        MulBase7FoldNoWrapVec(value, activeFold28_, tmp0, tmp1, count);
        AscendC::Add(value, value, addend, count);
        wrap_mod_vec_runtime(value, value, activeQ_, tmp0, tmp1, count);
        return;
    }
    MulBaseModVecRuntime(value, activeQ_, activeFold28_, tmp0, tmp1, count);
    AscendC::Add(value, value, addend, count);
    ParallelBatReduce(value, tmp0, tmp1, count, fastFold);
}

__aicore__ inline void CubeNtt::ComputePqcMerge(NttVectorBuffers<7>& buffers, const PqcPanel& panel)
{
    LocalTensor<int32_t> digit0 = buffers.Get<int32_t>(0);
    LocalTensor<int32_t> digit1 = buffers.Get<int32_t>(1);
    LocalTensor<int32_t> digit2 = buffers.Get<int32_t>(2);
    LocalTensor<int32_t> result = buffers.Get<int32_t>(3);
    LocalTensor<int32_t> partial = buffers.Get<int32_t>(4);
    LocalTensor<int32_t> tmp0 = buffers.Get<int32_t>(5);
    LocalTensor<int32_t> tmp1 = buffers.Get<int32_t>(6);

    const int32_t count = kParallelAivPolyBatch * panel.panelWidth;
    ParallelBatLoadDigitSum(digit0, partial, 0, panel.polyStart, panel.coeffStart, panel.panelWidth, panel.n);
    ParallelBatLoadDigitSum(digit1, partial, 1, panel.polyStart, panel.coeffStart, panel.panelWidth, panel.n);

    if (activeQ_ == 3329) {
        AscendC::Muls(result, digit1, 1, count);
        ParallelBatReduce(result, tmp0, tmp1, count, false);
        ParallelBatMergeStep(result, digit0, tmp0, tmp1, count, false);
    } else {
        ParallelBatLoadDigitSum(digit2, partial, 2, panel.polyStart, panel.coeffStart, panel.panelWidth, panel.n);
        AscendC::Muls(result, digit2, 1, count);
        ParallelBatReduceSignedDsa(result, tmp0, tmp1, count);
        ParallelBatMergeStepDsa(result, digit1, tmp0, tmp1, count);
        ParallelBatMergeStepDsa(result, digit0, tmp0, tmp1, count);
    }
    AscendC::PipeBarrier<PIPE_ALL>();
}

__aicore__ inline void CubeNtt::ParallelBatMerge(__gm__ int32_t* dstBase, int32_t forcedTile)
{
    const auto panel = MakePqcPanel(forcedTile);
    if (!panel.valid) {
        return;
    }
    NttVectorBuffers<7> buffers;
    buffers.Init(panel.tileLength);
    ComputePqcMerge(buffers, panel);
    LocalTensor<int32_t> result = buffers.Get<int32_t>(3);
    if (panel.validRows > 0) {
        AscendC::GlobalTensor<int32_t> dst;
        dst.SetGlobalBuffer(
            dstBase + static_cast<size_t>(panel.polyStart) * panel.n + panel.coeffStart,
            static_cast<size_t>(panel.validRows) * panel.n);
        AscendC::DataCopyParams copyOut{
            static_cast<uint16_t>(panel.validRows), static_cast<uint16_t>(panel.panelWidth * sizeof(int32_t) / 32), 0,
            static_cast<uint16_t>((panel.n - panel.panelWidth) * sizeof(int32_t) / 32)};
        AscendC::DataCopy(dst, result, copyOut);
        AscendC::PipeBarrier<PIPE_ALL>();
    }
}
