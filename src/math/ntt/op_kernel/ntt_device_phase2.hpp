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

__aicore__ inline GM_ADDR CubeNtt::MatrixPhase2ForChannel(int32_t channel) const
{
    return matrixPhase2_ + static_cast<size_t>(channel) * BatMatrixBytesInt(tiling_.phase2KernelN);
}

__aicore__ inline RowPairWork CubeNtt::MakeRowPairWork() const
{
    constexpr int32_t rowPanels = 2;
    const int32_t worker = logicalBlockId_ * kMixAivPerAic + subBlockId_;
    const int32_t baseWorkers = aivCoreCount_ / rowPanels;
    const int32_t extraWorkers = aivCoreCount_ - baseWorkers * rowPanels;
    int32_t cursor = 0;
    RowPairWork work;
    for (int32_t row = 0; row < rowPanels; ++row) {
        const int32_t workers = baseWorkers + (row < extraWorkers ? 1 : 0);
        if (workers > 0 && worker >= cursor && worker < cursor + workers) {
            const int32_t polys = CeilDivInt(tiling_.polyCount, workers);
            work.rowStart = row * kTransposePanel;
            work.polyStart = (worker - cursor) * polys;
            work.polyEnd = MinInt(tiling_.polyCount, work.polyStart + polys);
            work.active = work.polyStart < work.polyEnd;
            return work;
        }
        cursor += workers;
    }
    return work;
}

__aicore__ inline void CubeNtt::InitRowPairTables(RowPairBuffers& b)
{
    constexpr int32_t panel = 32;
    for (int32_t i = 0; i < panel; ++i) {
        for (int32_t j = 0; j < panel; ++j) {
            b.transpose.SetValue(j * panel + i, static_cast<uint32_t>((i * panel + j) * sizeof(int32_t)));
            b.select[0].SetValue(i * panel + j, static_cast<uint32_t>((i * 64 + j) * sizeof(int32_t)));
            b.select[1].SetValue(i * panel + j, static_cast<uint32_t>((i * 64 + panel + j) * sizeof(int32_t)));
        }
    }
}

__aicore__ inline void CubeNtt::LoadRowPairTwiddles(RowPairBuffers& b, int32_t rowStart)
{
    constexpr int32_t count = 32 * 32;
    AscendC::GlobalTensor<int32_t> twiddle;
    twiddle.SetGlobalBuffer(activeTwiddlePhase12Base_ + static_cast<size_t>(rowStart) * 64, 32 * 64);
    AscendC::DataCopy(b.fullInput, twiddle, 32 * 64);
    AscendC::PipeBarrier<PIPE_ALL>();
    for (int32_t half = 0; half < 2; ++half) {
        AscendC::Gather(b.twiddle, b.fullInput, b.select[half], 0U, static_cast<uint32_t>(count));
        AscendC::PipeBarrier<PIPE_ALL>();
        const int32_t digit = 4 + 3 * half;
        ExtractInt10DigitVec(b.temp[digit], b.twiddle, 0, b.temp[12], count);
        ExtractInt10DigitVec(b.temp[digit + 1], b.twiddle, kFastDigitBits, b.temp[12], count);
        ExtractInt10DigitVec(b.temp[digit + 2], b.twiddle, kFastDigitBits * 2, b.temp[12], count);
    }
}

__aicore__ inline void CubeNtt::ProcessRowPairHalf(
    RowPairBuffers& b, size_t polyOffset, int32_t rowStart, int32_t half)
{
    constexpr int32_t count = 32 * 32;
    const int32_t digit = 4 + 3 * half;
    AscendC::Gather(b.input, b.fullInput, b.select[half], 0U, static_cast<uint32_t>(count));
    AscendC::PipeBarrier<PIPE_ALL>();
    MulModVecVecInt10SchoolbookPresplitRhs(
        b.product, b.input, activeQ_, activeFold28_, b.temp[digit], b.temp[digit + 1], b.temp[digit + 2],
        b.temp[0], b.temp[1], b.temp[2], b.temp[3], b.temp[10], b.temp[11], b.temp[12], b.temp[11],
        activeUseInt10TwoWrap_, activeUseInt10OneWrap_, count);
    AscendC::PipeBarrier<PIPE_ALL>();
    AscendC::Gather(b.output, b.product, b.transpose, 0U, static_cast<uint32_t>(count));
    AscendC::PipeBarrier<PIPE_ALL>();
    for (int32_t j = 0; j < 32; ++j) {
        AscendC::GlobalTensor<int32_t> dst;
        dst.SetGlobalBuffer(
            workPhase2Base_ + polyOffset + static_cast<size_t>(half * 32 + j) * 64 + rowStart, 32);
        AscendC::DataCopy(dst, b.output[j * 32], 32);
    }
}

__aicore__ inline void CubeNtt::ProcessRowPairPolynomial(
    RowPairBuffers& b, int32_t poly, int32_t rowStart)
{
    const size_t offset = static_cast<size_t>(poly) * 64 * 64;
    AscendC::GlobalTensor<int32_t> src;
    src.SetGlobalBuffer(workPhase1Base_ + offset + static_cast<size_t>(rowStart) * 64, 32 * 64);
    AscendC::DataCopy(b.fullInput, src, 32 * 64);
    AscendC::PipeBarrier<PIPE_ALL>();
    ProcessRowPairHalf(b, offset, rowStart, 0);
    ProcessRowPairHalf(b, offset, rowStart, 1);
    AscendC::PipeBarrier<PIPE_ALL>();
}

__aicore__ inline void CubeNtt::PreparePhase2Input64x64RowPairInt10()
{
    const auto work = MakeRowPairWork();
    if (!work.active) {
        return;
    }
    RowPairBuffers buffers;
    buffers.Init(32 * 32);
    InitRowPairTables(buffers);
    LoadRowPairTwiddles(buffers, work.rowStart);
    for (int32_t poly = work.polyStart; poly < work.polyEnd; ++poly) {
        ProcessRowPairPolynomial(buffers, poly, work.rowStart);
    }
}

__aicore__ inline PanelWork CubeNtt::MakePanelWork() const
{
    PanelWork work;
    const int32_t rowPanels = CeilDivInt(tiling_.n1, kTransposePanel);
    work.colPanelCount = CeilDivInt(tiling_.n2, kTransposePanel);
    if (rowPanels <= 0 || work.colPanelCount <= 0 || aivCoreCount_ <= 0) {
        return work;
    }
    work.panelsPerPoly = rowPanels * work.colPanelCount;
    const int32_t total = tiling_.polyCount * work.panelsPerPoly;
    const int32_t perWorker = CeilDivInt(total, aivCoreCount_);
    const int32_t worker = logicalBlockId_ * kMixAivPerAic + subBlockId_;
    work.panelStart = worker * perWorker;
    work.panelEnd = MinInt(total, work.panelStart + perWorker);
    work.active = work.panelStart < work.panelEnd;
    return work;
}

__aicore__ inline void CubeNtt::LoadPhase2Panel(
    LocalTensor<int32_t>& dst, __gm__ int32_t* base, const PanelRect& rect)
{
    constexpr int32_t count = kTransposePanel * kTransposePanel;
    if (rect.rows != kTransposePanel || rect.cols != kTransposePanel) {
        AscendC::Duplicate(dst, static_cast<int32_t>(0), count);
    }
    for (int32_t i = 0; i < rect.rows; ++i) {
        const size_t offset = static_cast<size_t>(rect.rowStart + i) * tiling_.n2 + rect.colStart;
        AscendC::GlobalTensor<int32_t> src;
        src.SetGlobalBuffer(base + offset, rect.cols);
        if (rect.cols % 8 == 0) {
            AscendC::DataCopy(dst[i * kTransposePanel], src, rect.cols);
        } else {
            for (int32_t j = 0; j < rect.cols; ++j) {
                dst.SetValue(i * kTransposePanel + j, src.GetValue(j));
            }
        }
    }
}

__aicore__ inline void CubeNtt::StorePhase2Panel(
    LocalTensor<int32_t>& src, size_t polyOffset, const PanelRect& rect)
{
    for (int32_t j = 0; j < rect.cols; ++j) {
        AscendC::GlobalTensor<int32_t> dst;
        dst.SetGlobalBuffer(
            workPhase2Base_ + polyOffset + static_cast<size_t>(rect.colStart + j) * tiling_.n1 + rect.rowStart,
            rect.rows);
        if (rect.rows % 8 == 0) {
            AscendC::DataCopy(dst, src[j * kTransposePanel], rect.rows);
        } else {
            for (int32_t i = 0; i < rect.rows; ++i) {
                dst.SetValue(i, src.GetValue(j * kTransposePanel + i));
            }
        }
    }
}

__aicore__ inline void CubeNtt::SplitPhase2Twiddles(Phase2Buffers& b)
{
    constexpr int32_t count = kTransposePanel * kTransposePanel;
    if (activeUseInt10_) {
        ExtractInt10DigitVec(b.temp[4], b.twiddle, 0, b.temp[12], count);
        ExtractInt10DigitVec(b.temp[5], b.twiddle, kFastDigitBits, b.temp[12], count);
        ExtractInt10DigitVec(b.temp[6], b.twiddle, kFastDigitBits * 2, b.temp[12], count);
    } else if (activeBarrettK_ > kDefaultBarrettK) {
        ExtractInt7DigitVec(b.temp[4], b.twiddle, 0, b.temp[12], count);
        ExtractInt7DigitVec(b.temp[5], b.twiddle, kDigitBaseBits, b.temp[12], count);
        ExtractInt7DigitVec(b.temp[6], b.twiddle, kDigitBaseBits * 2, b.temp[12], count);
        ExtractInt7DigitVec(b.temp[7], b.twiddle, kDigitBaseBits * 3, b.temp[12], count);
    }
}

__aicore__ inline void CubeNtt::MultiplyPhase2Panel(Phase2Buffers& b)
{
    constexpr int32_t count = kTransposePanel * kTransposePanel;
    if (activeBarrettK_ <= kDefaultBarrettK) {
        MulModVecVecInt7Horner(b.product, b.input, b.twiddle, activeQ_, activeBarrettK_,
            static_cast<int32_t>(activeBarrettMu_), b.temp[0], b.temp[1], b.temp[2], b.temp[3], b.temp[4], count);
    } else if (activeUseInt10_) {
        MulModVecVecInt10SchoolbookPresplitRhs(
            b.product, b.input, activeQ_, activeFold28_, b.temp[4], b.temp[5], b.temp[6], b.temp[0], b.temp[1],
            b.temp[2], b.temp[8], b.temp[9], b.temp[10], b.temp[11], b.temp[12],
            activeUseInt10TwoWrap_, activeUseInt10OneWrap_, count);
    } else {
        MulModVecVecInt7SchoolbookPresplitRhs(
            b.product, b.input, activeQ_, activeFold28_, b.temp[4], b.temp[5], b.temp[6], b.temp[7],
            b.temp[0], b.temp[1], b.temp[2], b.temp[3], b.temp[8], b.temp[9], b.temp[10], b.temp[11],
            b.temp[12], count);
    }
}

__aicore__ inline void CubeNtt::RunPhase2Panel(Phase2Buffers& b, size_t polyOffset, const PanelRect& rect)
{
    LoadPhase2Panel(b.input, workPhase1Base_ + polyOffset, rect);
    AscendC::PipeBarrier<PIPE_ALL>();
    MultiplyPhase2Panel(b);
    AscendC::PipeBarrier<PIPE_ALL>();
    AscendC::Gather(b.output, b.product, b.transpose, 0U,
        static_cast<uint32_t>(kTransposePanel * kTransposePanel));
    AscendC::PipeBarrier<PIPE_ALL>();
    StorePhase2Panel(b.output, polyOffset, rect);
    AscendC::PipeBarrier<PIPE_ALL>();
}

__aicore__ inline void CubeNtt::PrepareLocalPanel(Phase2Buffers& b, const PanelWork& work, int32_t localPanel)
{
    if (work.colPanelCount <= 0 || work.panelsPerPoly <= 0) {
        return;
    }
    int32_t first = localPanel;
    if (first < work.panelStart) {
        first += CeilDivInt(work.panelStart - first, work.panelsPerPoly) * work.panelsPerPoly;
    }
    if (first >= work.panelEnd) {
        return;
    }
    const int32_t row = localPanel / work.colPanelCount;
    const int32_t col = localPanel - row * work.colPanelCount;
    const PanelRect rect{row * kTransposePanel, col * kTransposePanel,
        MinInt(kTransposePanel, tiling_.n1 - row * kTransposePanel),
        MinInt(kTransposePanel, tiling_.n2 - col * kTransposePanel)};
    LoadPhase2Panel(b.twiddle, activeTwiddlePhase12Base_, rect);
    AscendC::PipeBarrier<PIPE_ALL>();
    SplitPhase2Twiddles(b);
    for (int32_t panel = first; panel < work.panelEnd; panel += work.panelsPerPoly) {
        const int32_t poly = panel / work.panelsPerPoly;
        RunPhase2Panel(b, static_cast<size_t>(poly) * tiling_.n, rect);
    }
}

__aicore__ inline void CubeNtt::InitTransposeTable(LocalTensor<uint32_t>& lut)
{
    for (int32_t i = 0; i < kTransposePanel; ++i) {
        for (int32_t j = 0; j < kTransposePanel; ++j) {
            lut.SetValue(j * kTransposePanel + i,
                static_cast<uint32_t>((i * kTransposePanel + j) * sizeof(int32_t)));
        }
    }
}

__aicore__ inline void CubeNtt::PreparePhase2Input()
{
    if (isAic_) {
        return;
    }
    if (tiling_.n1 == 64 && tiling_.n2 == 64 && kTransposePanel == 32 &&
        aivCoreCount_ >= 4 && activeUseInt10_) {
        PreparePhase2Input64x64RowPairInt10();
        return;
    }
    const auto work = MakePanelWork();
    if (!work.active) {
        return;
    }
    Phase2Buffers buffers;
    buffers.Init(kTransposePanel * kTransposePanel);
    InitTransposeTable(buffers.transpose);
    for (int32_t panel = 0; panel < work.panelsPerPoly; ++panel) {
        PrepareLocalPanel(buffers, work, panel);
    }
}
