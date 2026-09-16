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

#include "ntt_macros.h"
#include "ntt_tiling_data.h"
#include "kernel_operator.h"
#include "lib/matmul_intf.h"
#include "aic_func.hpp"
#include "cube_math.hpp"
#include "ntt_device_buffers.hpp"

#include <cstdint>

using AscendC::LocalTensor;

constexpr int32_t kMixAivPerAic = 2;
constexpr int32_t kSyncIntsPerCore = 8;
// Keep custom AIC/AIV handshakes out of SyncAll<false>()'s reserved ffts
// flags 11..14. Cross-core state ids are local to each AIC/AIV core pair, so
// all logical blocks can reuse the pipeline slots without consuming blockDim states.
constexpr int32_t kFirstSyncAllReservedFlag = 11;
constexpr int32_t kSubNttPipelineSlots = static_cast<int32_t>(tiling_one::sub_ntt_pipeline_slots);
constexpr uint16_t kSerialCrossCoreStates = 1;
constexpr uint16_t kPipelinedCrossCoreStates = static_cast<uint16_t>(kSubNttPipelineSlots);
constexpr int32_t kDefaultBarrettK = 23;
constexpr int32_t kDigitBaseBits = 7;
constexpr int32_t kDigitBase = 1 << kDigitBaseBits;
constexpr int32_t kDigitMax = kDigitBase - 1;
constexpr int32_t kDigitCount = 4;
constexpr int32_t kResidueBits = kDigitBaseBits * kDigitCount;
constexpr int32_t kResidueBase = 1 << kResidueBits;
constexpr int32_t kFastFold28Limit = kResidueBase / 129;
constexpr int32_t kFastDigitBits = 10;
constexpr int32_t kFastDigitBase = 1 << kFastDigitBits;
constexpr int32_t kFastDigitHighShift = kResidueBits - kFastDigitBits;
// Keeps (x * 2^10 folded by 2^28 == fold28) plus one 10-bit digit product below 2q.
constexpr int32_t kFastFold10Limit = (kResidueBase - 2 * kFastDigitBase * kFastDigitBase) / (kFastDigitBase + 1);
constexpr int32_t kFastFold10TwoWrapLimit = 200000;
constexpr int32_t kFastFold10OneWrapLimit = 150000;
constexpr int32_t kBatTileCount = kDigitCount * kDigitCount; // [out_chunk][in_chunk]
constexpr int32_t kTransposePanel = 32;
constexpr int32_t kParallelAivPolyBatch = 16;

__aicore__ inline size_t MaxSize(size_t a, size_t b) { return a > b ? a : b; }

__aicore__ inline int32_t CeilDivInt(int32_t a, int32_t b)
{
    if (b == 0) {
        return 0;
    }
    if (b < 0) {
        return 0;
    }
    return (a + b - 1) / b;
}

__aicore__ inline int32_t AlignUpInt(int32_t value, int32_t align)
{
    if (align == 0) {
        return 0;
    }
    if (align < 0) {
        return 0;
    }
    return ((value + align - 1) / align) * align;
}

__aicore__ inline int32_t SplitBenchMultipleInt(int32_t kernelN)
{
    const int32_t splitBytesPerBench = kernelN / 2;
    if (splitBytesPerBench <= 0) {
        return 1;
    }
    return (splitBytesPerBench >= 32) ? 1 : (32 / splitBytesPerBench);
}

__aicore__ inline int32_t PaddedBenchInt(int32_t kernelN, int32_t logicalBench)
{
    int32_t aligned = AlignUpInt(logicalBench, SplitBenchMultipleInt(kernelN));
    return aligned < 4 ? 4 : aligned;
}

__aicore__ inline size_t ScratchBytesInt(int32_t kernelN, int32_t bench)
{
    const size_t splitBytes = static_cast<size_t>(4) * kernelN * bench * sizeof(int8_t);
    const size_t accumBytes = static_cast<size_t>(4) * kernelN * bench * sizeof(int32_t);
    return splitBytes + accumBytes;
}

__aicore__ inline size_t BatMatrixBytesInt(int32_t kernelN)
{
    return static_cast<size_t>(kBatTileCount) * static_cast<size_t>(kernelN) * static_cast<size_t>(kernelN) *
           sizeof(int8_t);
}

__aicore__ inline int32_t EffectiveRnsChannels(const NttTilingData& tiling)
{
    return tiling.rnsChannels > 0 ? tiling.rnsChannels : 1;
}

__aicore__ inline int32_t EffectiveChannelBlock(const NttTilingData& tiling)
{
    const int32_t rnsChannels = EffectiveRnsChannels(tiling);
    const int32_t configured = tiling.channelBlock > 0 ? tiling.channelBlock : 1;
    return configured < rnsChannels ? configured : rnsChannels;
}

__aicore__ inline size_t StageScratchBytesInt(int32_t kernelN, int32_t subBench, int32_t blockDim)
{
    return ScratchBytesInt(kernelN, PaddedBenchInt(kernelN, subBench)) * static_cast<size_t>(blockDim) *
           static_cast<size_t>(kSubNttPipelineSlots);
}

__aicore__ inline size_t DirectPqcScratchBytesInt(const NttTilingData& tiling)
{
    const int32_t paddedPolyCount = AlignUpInt(tiling.polyCount, tiling.subBench);
    const size_t planeBytes = static_cast<size_t>(paddedPolyCount) * tiling.n;
    return static_cast<size_t>(kDigitCount) * planeBytes +
           static_cast<size_t>(kBatTileCount) * planeBytes * sizeof(int32_t);
}

__aicore__ inline bool IsDirectPqcTiling(const NttTilingData& tiling)
{
    const int32_t q = tiling.qList[0] > 0 ? tiling.qList[0] : tiling.q;
    return tiling.n == 256 && tiling.phase1KernelN == 256 && tiling.rnsChannels == 1 && tiling.channelBlock == 1 &&
           tiling.blockDim == kBatTileCount && tiling.subBench == 16 && (q == 3329 || q == 8380417);
}

__aicore__ inline FourStepWorkspaceLayout MakeWorkspaceLayoutDevice(const NttTilingData& tiling)
{
    const size_t channelBlock = static_cast<size_t>(EffectiveChannelBlock(tiling));
    const size_t logicalBytes = static_cast<size_t>(tiling.polyCount) * tiling.n * sizeof(int32_t);
    const size_t packedBytes = 0;
    const size_t stageScratchBytes = MaxSize(
        StageScratchBytesInt(tiling.phase1KernelN, tiling.subBench, tiling.blockDim),
        StageScratchBytesInt(tiling.phase2KernelN, tiling.subBench, tiling.blockDim));
    const size_t scratchBytes =
        IsDirectPqcTiling(tiling) ? MaxSize(stageScratchBytes, DirectPqcScratchBytesInt(tiling)) : stageScratchBytes;
    const size_t syncBytes = static_cast<size_t>(tiling.blockDim) * static_cast<size_t>(kMixAivPerAic) *
                             static_cast<size_t>(kSyncIntsPerCore) * sizeof(int32_t);

    FourStepWorkspaceLayout layout{};
    layout.workPhase1Offset = 0;
    layout.workPhase2Offset = layout.workPhase1Offset + logicalBytes * channelBlock;
    layout.packedOffset = layout.workPhase2Offset + logicalBytes * channelBlock;
    layout.scratchOffset = layout.packedOffset + packedBytes * channelBlock;
    layout.syncOffset = layout.scratchOffset + scratchBytes * channelBlock;
    layout.totalBytes = layout.syncOffset + syncBytes * channelBlock;
    return layout;
}

__aicore__ inline void __WAIT(uint16_t stateID) { AscendC::CrossCoreWaitFlag<2, PIPE_MTE2>(stateID); }

__aicore__ inline void __SET(uint16_t stateID, bool isAic)
{
    if (isAic) {
        AscendC::PipeBarrier<PIPE_ALL>();
    } else {
        AscendC::PipeBarrier<PIPE_MTE3>();
    }
    AscendC::CrossCoreSetFlag<2, PIPE_MTE2>(stateID);
}

__aicore__ inline int32_t MinInt(int32_t a, int32_t b) { return a < b ? a : b; }

__aicore__ inline int32_t ComputeBarrettK(int32_t mod)
{
    uint32_t value = static_cast<uint32_t>(mod - 1);
    int32_t bits = 0;
    while (value > 0) {
        ++bits;
        value >>= 1;
    }
    return bits > 1 ? bits : 2;
}

__aicore__ inline uint64_t ComputeBarrettMu(int32_t mod, int32_t barrettK)
{
    return (static_cast<uint64_t>(1) << (2 * barrettK)) / static_cast<uint64_t>(static_cast<uint32_t>(mod));
}

__aicore__ inline void ExtractInt7DigitVec(
    LocalTensor<int32_t>& digit, LocalTensor<int32_t>& value, int32_t shift, LocalTensor<int32_t>& tmp, int32_t count)
{
    if (shift == 0) {
        AscendC::Muls(digit, value, 1, count);
    } else {
        ShiftRightU(digit, value, shift, count);
    }
    ShiftRightU(tmp, digit, kDigitBaseBits, count);
    AscendC::ShiftLeft(tmp, tmp, kDigitBaseBits, count);
    AscendC::Sub(digit, digit, tmp, count);
}

__aicore__ inline bool CanUseInt10Fold(int32_t fold28) { return fold28 > 0 && fold28 < kFastFold10Limit; }

__aicore__ inline bool CanSkipInt10FirstWrap(int32_t fold28) { return fold28 > 0 && fold28 < kFastFold10TwoWrapLimit; }

__aicore__ inline bool CanSkipInt10SecondWrap(int32_t fold28) { return fold28 > 0 && fold28 < kFastFold10OneWrapLimit; }

__aicore__ inline bool CanUseBase7FoldWithAdd(int32_t fold28, int32_t maxProductTerms)
{
    const int32_t limit = (kResidueBase - maxProductTerms * kDigitMax * kDigitMax) / (kDigitBase + 1);
    return fold28 > 0 && fold28 < limit;
}

__aicore__ inline void ExtractInt10DigitVec(
    LocalTensor<int32_t>& digit, LocalTensor<int32_t>& value, int32_t shift, LocalTensor<int32_t>& tmp, int32_t count)
{
    if (shift == 0) {
        AscendC::Muls(digit, value, 1, count);
    } else {
        ShiftRightU(digit, value, shift, count);
    }
    if (shift < kFastDigitBits * 2) {
        ShiftRightU(tmp, digit, kFastDigitBits, count);
        AscendC::ShiftLeft(tmp, tmp, kFastDigitBits, count);
        AscendC::Sub(digit, digit, tmp, count);
    }
}

__aicore__ inline void MulBase10FoldNoWrapVec(
    LocalTensor<int32_t>& x, int32_t fold28, LocalTensor<int32_t>& tmp0, LocalTensor<int32_t>& tmp1, int32_t count)
{
    ShiftRightU(tmp0, x, kFastDigitHighShift, count);
    AscendC::ShiftLeft(tmp1, tmp0, kFastDigitHighShift, count);
    AscendC::Sub(tmp1, x, tmp1, count);
    AscendC::ShiftLeft(tmp1, tmp1, kFastDigitBits, count);
    AscendC::Muls(tmp0, tmp0, fold28, count);
    AscendC::Add(x, tmp1, tmp0, count);
}

__aicore__ inline void MulBase7FoldNoWrapVec(
    LocalTensor<int32_t>& x, int32_t fold28, LocalTensor<int32_t>& tmp0, LocalTensor<int32_t>& tmp1, int32_t count)
{
    ShiftRightU(tmp0, x, kDigitBaseBits * 3, count);
    AscendC::ShiftLeft(tmp1, tmp0, kDigitBaseBits * 3, count);
    AscendC::Sub(tmp1, x, tmp1, count);
    AscendC::ShiftLeft(tmp1, tmp1, kDigitBaseBits, count);
    AscendC::Muls(tmp0, tmp0, fold28, count);
    AscendC::Add(x, tmp1, tmp0, count);
}

__aicore__ inline void MulModVecVecInt7Horner(
    LocalTensor<int32_t>& dst, LocalTensor<int32_t>& src, LocalTensor<int32_t>& rhs, int32_t q, int32_t barrettK,
    int32_t barrettMu, LocalTensor<int32_t>& digit, LocalTensor<int32_t>& term, LocalTensor<int32_t>& tmp0,
    LocalTensor<int32_t>& tmp1, LocalTensor<int32_t>& tmp2, int32_t count)
{
    ExtractInt7DigitVec(digit, rhs, kDigitBaseBits * 3, tmp2, count);
    AscendC::Mul(dst, src, digit, count);
    barrett_mul_vec_runtime(dst, q, barrettK, barrettMu, tmp0, tmp1, count);

    for (int32_t digitIdx = 2; digitIdx >= 0; --digitIdx) {
        AscendC::Muls(dst, dst, kDigitBase, count);
        barrett_mul_vec_runtime(dst, q, barrettK, barrettMu, tmp0, tmp1, count);
        ExtractInt7DigitVec(digit, rhs, kDigitBaseBits * digitIdx, tmp2, count);
        AscendC::Mul(term, src, digit, count);
        AscendC::Add(dst, dst, term, count);
        barrett_mul_vec_runtime(dst, q, barrettK, barrettMu, tmp0, tmp1, count);
    }
}

__aicore__ inline void MulBaseModVecRuntime(
    LocalTensor<int32_t>& x, int32_t q, int32_t fold28, LocalTensor<int32_t>& tmp0, LocalTensor<int32_t>& tmp1,
    int32_t count)
{
    if (fold28 > 0 && fold28 < kFastFold28Limit) {
        MulBase7FoldNoWrapVec(x, fold28, tmp0, tmp1, count);
        wrap_mod_vec_runtime(x, x, q, tmp0, tmp1, count);
        return;
    }

    for (int32_t bit = 0; bit < kDigitBaseBits; ++bit) {
        AscendC::Add(x, x, x, count);
        wrap_mod_vec_runtime(x, x, q, tmp0, tmp1, count);
    }
}

__aicore__ inline void AccumulateDigitProduct(
    LocalTensor<int32_t>& sum, LocalTensor<int32_t>& lhsDigit, LocalTensor<int32_t>& rhsDigit,
    LocalTensor<int32_t>& term, int32_t count)
{
    AscendC::Mul(term, lhsDigit, rhsDigit, count);
    AscendC::Add(sum, sum, term, count);
}

__aicore__ inline void MulModVecVecInt7SchoolbookPresplitRhs(
    LocalTensor<int32_t>& dst, LocalTensor<int32_t>& lhs, int32_t q, int32_t fold28, LocalTensor<int32_t>& rhs0,
    LocalTensor<int32_t>& rhs1, LocalTensor<int32_t>& rhs2, LocalTensor<int32_t>& rhs3, LocalTensor<int32_t>& lhs0,
    LocalTensor<int32_t>& lhs1, LocalTensor<int32_t>& lhs2, LocalTensor<int32_t>& lhs3, LocalTensor<int32_t>& digitSum,
    LocalTensor<int32_t>& term, LocalTensor<int32_t>& reduce0, LocalTensor<int32_t>& reduce1,
    LocalTensor<int32_t>& extractTmp, int32_t count)
{
    ExtractInt7DigitVec(lhs0, lhs, 0, extractTmp, count);
    ExtractInt7DigitVec(lhs1, lhs, kDigitBaseBits, extractTmp, count);
    ExtractInt7DigitVec(lhs2, lhs, kDigitBaseBits * 2, extractTmp, count);
    ExtractInt7DigitVec(lhs3, lhs, kDigitBaseBits * 3, extractTmp, count);

    AscendC::Mul(dst, lhs3, rhs3, count);
    for (int32_t degree = 5; degree >= 0; --degree) {
        if (degree == 5 || (degree == 4 && fold28 > 0 && fold28 < kFastFold28Limit)) {
            AscendC::ShiftLeft(dst, dst, kDigitBaseBits, count);
        } else {
            MulBaseModVecRuntime(dst, q, fold28, reduce0, reduce1, count);
        }
        if (degree == 5) {
            AscendC::Mul(digitSum, lhs2, rhs3, count);
            AccumulateDigitProduct(digitSum, lhs3, rhs2, term, count);
        } else if (degree == 4) {
            AscendC::Mul(digitSum, lhs1, rhs3, count);
            AccumulateDigitProduct(digitSum, lhs2, rhs2, term, count);
            AccumulateDigitProduct(digitSum, lhs3, rhs1, term, count);
        } else if (degree == 3) {
            AscendC::Mul(digitSum, lhs0, rhs3, count);
            AccumulateDigitProduct(digitSum, lhs1, rhs2, term, count);
            AccumulateDigitProduct(digitSum, lhs2, rhs1, term, count);
            AccumulateDigitProduct(digitSum, lhs3, rhs0, term, count);
        } else if (degree == 2) {
            AscendC::Mul(digitSum, lhs0, rhs2, count);
            AccumulateDigitProduct(digitSum, lhs1, rhs1, term, count);
            AccumulateDigitProduct(digitSum, lhs2, rhs0, term, count);
        } else if (degree == 1) {
            AscendC::Mul(digitSum, lhs0, rhs1, count);
            AccumulateDigitProduct(digitSum, lhs1, rhs0, term, count);
        } else {
            AscendC::Mul(digitSum, lhs0, rhs0, count);
        }
        AscendC::Add(dst, dst, digitSum, count);
        if (degree != 5) {
            wrap_mod_vec_runtime(dst, dst, q, reduce0, reduce1, count);
        }
    }
}

__aicore__ inline void MulModVecVecInt10SchoolbookPresplitRhs(
    LocalTensor<int32_t>& dst, LocalTensor<int32_t>& lhs, int32_t q, int32_t fold28, LocalTensor<int32_t>& rhs0,
    LocalTensor<int32_t>& rhs1, LocalTensor<int32_t>& rhs2, LocalTensor<int32_t>& lhs0, LocalTensor<int32_t>& lhs1,
    LocalTensor<int32_t>& lhs2, LocalTensor<int32_t>& digitSum, LocalTensor<int32_t>& term,
    LocalTensor<int32_t>& reduce0, LocalTensor<int32_t>& reduce1, LocalTensor<int32_t>& extractTmp, bool skipFirstWrap,
    bool skipSecondWrap, int32_t count)
{
    ExtractInt10DigitVec(lhs0, lhs, 0, extractTmp, count);
    ExtractInt10DigitVec(lhs1, lhs, kFastDigitBits, extractTmp, count);
    ExtractInt10DigitVec(lhs2, lhs, kFastDigitBits * 2, extractTmp, count);

    AscendC::Mul(dst, lhs2, rhs2, count);

    AscendC::ShiftLeft(dst, dst, kFastDigitBits, count);
    AscendC::Mul(digitSum, lhs1, rhs2, count);
    AccumulateDigitProduct(digitSum, lhs2, rhs1, term, count);
    AscendC::Add(dst, dst, digitSum, count);

    MulBase10FoldNoWrapVec(dst, fold28, reduce0, reduce1, count);
    AscendC::Mul(digitSum, lhs0, rhs2, count);
    AccumulateDigitProduct(digitSum, lhs1, rhs1, term, count);
    AccumulateDigitProduct(digitSum, lhs2, rhs0, term, count);
    AscendC::Add(dst, dst, digitSum, count);
    if (!skipFirstWrap) {
        wrap_mod_vec_runtime(dst, dst, q, reduce0, reduce1, count);
    }

    MulBase10FoldNoWrapVec(dst, fold28, reduce0, reduce1, count);
    AscendC::Mul(digitSum, lhs0, rhs1, count);
    AccumulateDigitProduct(digitSum, lhs1, rhs0, term, count);
    AscendC::Add(dst, dst, digitSum, count);
    if (!skipSecondWrap) {
        wrap_mod_vec_runtime(dst, dst, q, reduce0, reduce1, count);
    }

    MulBase10FoldNoWrapVec(dst, fold28, reduce0, reduce1, count);
    AscendC::Mul(digitSum, lhs0, rhs0, count);
    AscendC::Add(dst, dst, digitSum, count);
    wrap_mod_vec_runtime(dst, dst, q, reduce0, reduce1, count);
}
