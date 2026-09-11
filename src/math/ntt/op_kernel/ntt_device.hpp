/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ntt_macros.h"
#include "ntt_tiling_data.h"
#include "kernel_operator.h"
#include "lib/matmul_intf.h"
#include "aic_func.hpp"
#include "cube_math.hpp"

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

__aicore__ inline size_t MaxSize(size_t a, size_t b)
{
    return a > b ? a : b;
}

__aicore__ inline int32_t CeilDivInt(int32_t a, int32_t b)
{
    return (a + b - 1) / b;
}

__aicore__ inline int32_t AlignUpInt(int32_t value, int32_t align)
{
    return ((value + align - 1) / align) * align;
}

__aicore__ inline int32_t SplitBenchMultipleInt(int32_t kernelN)
{
    const int32_t splitBytesPerBench = kernelN / 2;
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

__aicore__ inline size_t DirectPqcScratchBytesInt(
    const NttTilingData& tiling)
{
    const int32_t paddedPolyCount =
        AlignUpInt(tiling.polyCount, tiling.subBench);
    const size_t planeBytes =
        static_cast<size_t>(paddedPolyCount) * tiling.n;
    return static_cast<size_t>(kDigitCount) * planeBytes +
           static_cast<size_t>(kBatTileCount) * planeBytes *
               sizeof(int32_t);
}

__aicore__ inline bool IsDirectPqcTiling(
    const NttTilingData& tiling)
{
    const int32_t q = tiling.qList[0] > 0 ? tiling.qList[0] : tiling.q;
    return tiling.n == 256 && tiling.phase1KernelN == 256 &&
           tiling.rnsChannels == 1 && tiling.channelBlock == 1 &&
           tiling.blockDim == kBatTileCount && tiling.subBench == 16 &&
           (q == 3329 || q == 8380417);
}

__aicore__ inline FourStepWorkspaceLayout MakeWorkspaceLayoutDevice(const NttTilingData& tiling)
{
    const size_t channelBlock = static_cast<size_t>(EffectiveChannelBlock(tiling));
    const size_t logicalBytes = static_cast<size_t>(tiling.polyCount) * tiling.n * sizeof(int32_t);
    const size_t packedBytes = 0;
    const size_t stageScratchBytes = MaxSize(
        StageScratchBytesInt(tiling.phase1KernelN, tiling.subBench, tiling.blockDim),
        StageScratchBytesInt(tiling.phase2KernelN, tiling.subBench, tiling.blockDim));
    const size_t scratchBytes = IsDirectPqcTiling(tiling)
        ? MaxSize(stageScratchBytes, DirectPqcScratchBytesInt(tiling))
        : stageScratchBytes;
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

__aicore__ inline void __WAIT(uint16_t stateID)
{
    AscendC::CrossCoreWaitFlag<2, PIPE_MTE2>(stateID);
}

__aicore__ inline void __SET(uint16_t stateID, bool isAic)
{
    if (isAic) {
        AscendC::PipeBarrier<PIPE_ALL>();
    } else {
        AscendC::PipeBarrier<PIPE_MTE3>();
    }
    AscendC::CrossCoreSetFlag<2, PIPE_MTE2>(stateID);
}

__aicore__ inline int32_t MinInt(int32_t a, int32_t b)
{
    return a < b ? a : b;
}

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

__aicore__ inline bool CanUseInt10Fold(int32_t fold28)
{
    return fold28 > 0 && fold28 < kFastFold10Limit;
}

__aicore__ inline bool CanSkipInt10FirstWrap(int32_t fold28)
{
    return fold28 > 0 && fold28 < kFastFold10TwoWrapLimit;
}

__aicore__ inline bool CanSkipInt10SecondWrap(int32_t fold28)
{
    return fold28 > 0 && fold28 < kFastFold10OneWrapLimit;
}

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

class CubeNtt {
public:
    __aicore__ inline void Init(
        GM_ADDR x, GM_ADDR z, GM_ADDR workspace, GM_ADDR matrixPhase1, GM_ADDR matrixPhase2, GM_ADDR twiddlePhase12,
        const NttTilingData& tiling)
    {
        tiling_ = tiling;
        isAic_ = (g_coreType == AscendC::AIC);
        subBlockId_ = static_cast<int32_t>(AscendC::GetSubBlockIdx());
        rnsChannels_ = EffectiveRnsChannels(tiling_);
        channelBlock_ = EffectiveChannelBlock(tiling_);
        logicalBlockCount_ = tiling_.blockDim;
        aivCoreCount_ = logicalBlockCount_ * kMixAivPerAic;
        const int32_t globalLogicalBlockId = isAic_ ? static_cast<int32_t>(AscendC::GetBlockIdx()) :
                                                      static_cast<int32_t>(AscendC::GetBlockIdx() / kMixAivPerAic);
        // Map the launch dimension as [channel lane][per-channel local block].
        channelLaneId_ = globalLogicalBlockId / logicalBlockCount_;
        logicalBlockId_ = globalLogicalBlockId - channelLaneId_ * logicalBlockCount_;
        active_ = channelLaneId_ < channelBlock_ && logicalBlockId_ < logicalBlockCount_;
        if (!active_) {
            return;
        }

        layout_ = MakeWorkspaceLayoutDevice(tiling_);
        const size_t logicalElemsPerChannel = static_cast<size_t>(tiling_.polyCount) * static_cast<size_t>(tiling_.n);
        const size_t phaseLaneBytes = static_cast<size_t>(channelLaneId_) * logicalElemsPerChannel * sizeof(int32_t);
        const size_t scratchBytesPerChannel = MaxSize(
            StageScratchBytesInt(tiling_.phase1KernelN, tiling_.subBench, tiling_.blockDim),
            StageScratchBytesInt(tiling_.phase2KernelN, tiling_.subBench, tiling_.blockDim));
        xBase_ = reinterpret_cast<__gm__ int32_t*>(x);
        zBase_ = reinterpret_cast<__gm__ int32_t*>(z);
        workPhase1Base_ = reinterpret_cast<__gm__ int32_t*>(workspace + layout_.workPhase1Offset + phaseLaneBytes);
        workPhase2Base_ = reinterpret_cast<__gm__ int32_t*>(workspace + layout_.workPhase2Offset + phaseLaneBytes);
        scratchBase_ = workspace + layout_.scratchOffset + static_cast<size_t>(channelLaneId_) * scratchBytesPerChannel;
        matrixPhase1_ = matrixPhase1;
        matrixPhase2_ = matrixPhase2;
        twiddlePhase12Base_ = reinterpret_cast<__gm__ int32_t*>(twiddlePhase12);
        SetActiveChannel(0);

        workPhase1Gm_.SetGlobalBuffer(workPhase1Base_, logicalElemsPerChannel);
        workPhase2Gm_.SetGlobalBuffer(workPhase2Base_, logicalElemsPerChannel);
    }

    __aicore__ inline void Process()
    {
        if (!active_) {
            return;
        }
        const int32_t channelGroupCount = CeilDivInt(rnsChannels_, channelBlock_);
        for (int32_t channelBlockId = 0; channelBlockId < channelGroupCount; ++channelBlockId) {
            const int32_t channel = channelBlockId * channelBlock_ + channelLaneId_;
            if (channel < rnsChannels_) {
                SetActiveChannel(channel);
                ProcessActiveChannel(channel);
            } else {
                ProcessInactiveChannel();
            }
            if (!tiling_.useFourStep) {
                AscendC::PipeBarrier<PIPE_ALL>();
                if (!UseParallelDirectBat()) {
                    SyncAllCores();
                }
            }
        }
    }

private:
    __aicore__ inline void ProcessActiveChannel(int32_t channel)
    {
        __gm__ int32_t* xChannelBase = xBase_ + RnsChannelElemOffset(channel);
        __gm__ int32_t* zChannelBase = zBase_ + RnsChannelElemOffset(channel);
        GM_ADDR matrixPhase1 = MatrixPhase1ForChannel(channel);
        GM_ADDR matrixPhase2 = MatrixPhase2ForChannel(channel);

        if (!tiling_.useFourStep) {
            if (UseParallelDirectBat()) {
                ProcessParallelDirectBat(
                    xChannelBase, zChannelBase, matrixPhase1);
                return;
            }
            SubNTT(
                xChannelBase, tiling_.n, zChannelBase, tiling_.n, tiling_.phase1KernelN, tiling_.n,
                tiling_.polyCount, matrixPhase1);
            return;
        }

        SubNTT(
            xChannelBase, tiling_.n2, workPhase1Base_, tiling_.n2, tiling_.phase1KernelN, tiling_.n2,
            tiling_.polyCount * tiling_.n1, matrixPhase1);

        AscendC::PipeBarrier<PIPE_ALL>();
        SyncAllCores();

        if (!isAic_) {
            AscendC::DataCacheCleanAndInvalid<
                int32_t, AscendC::CacheLine::ENTIRE_DATA_CACHE, AscendC::DcciDst::CACHELINE_OUT>(workPhase1Gm_);
        }
        AscendC::PipeBarrier<PIPE_ALL>();
        SyncAllCores();

        PreparePhase2Input();

        AscendC::PipeBarrier<PIPE_ALL>();
        if (!isAic_) {
            AscendC::DataCacheCleanAndInvalid<
                int32_t, AscendC::CacheLine::ENTIRE_DATA_CACHE, AscendC::DcciDst::CACHELINE_OUT>(workPhase2Gm_);
        }
        AscendC::PipeBarrier<PIPE_ALL>();
        SyncAllCores();

        SubNTT(
            workPhase2Base_, tiling_.n1, zChannelBase, tiling_.n1, tiling_.phase2KernelN, tiling_.n1,
            tiling_.polyCount * tiling_.n2, matrixPhase2);

        SyncAllCores();
    }

    __aicore__ inline void ProcessInactiveChannel()
    {
        if (!tiling_.useFourStep) {
            return;
        }

        AscendC::PipeBarrier<PIPE_ALL>();
        SyncAllCores();

        AscendC::PipeBarrier<PIPE_ALL>();
        SyncAllCores();

        AscendC::PipeBarrier<PIPE_ALL>();
        AscendC::PipeBarrier<PIPE_ALL>();
        SyncAllCores();

        SyncAllCores();
    }

    __aicore__ inline size_t RnsChannelElemOffset(int32_t channel) const
    {
        return static_cast<size_t>(channel) * static_cast<size_t>(tiling_.polyCount) * static_cast<size_t>(tiling_.n);
    }

    __aicore__ inline int32_t ChannelQ(int32_t channel) const
    {
        if (tiling_.rnsChannels > 0 && tiling_.qList[channel] > 0) {
            return tiling_.qList[channel];
        }
        return tiling_.q;
    }

    __aicore__ inline GM_ADDR MatrixPhase1ForChannel(int32_t channel) const
    {
        return matrixPhase1_ + static_cast<size_t>(channel) * BatMatrixBytesInt(tiling_.phase1KernelN);
    }

    __aicore__ inline GM_ADDR MatrixPhase2ForChannel(int32_t channel) const
    {
        return matrixPhase2_ + static_cast<size_t>(channel) * BatMatrixBytesInt(tiling_.phase2KernelN);
    }

    __aicore__ inline size_t TwiddleCountPerChannel() const
    {
        return tiling_.useFourStep ? static_cast<size_t>(tiling_.n1) * static_cast<size_t>(tiling_.n2) : 1;
    }

    __aicore__ inline void SetActiveChannel(int32_t channel)
    {
        activeQ_ = ChannelQ(channel);
        activeBarrettK_ = ComputeBarrettK(activeQ_);
        activeBarrettMu_ = ComputeBarrettMu(activeQ_, activeBarrettK_);
        activeFold28_ =
            (activeBarrettK_ == kResidueBits && activeQ_ > (kResidueBase / 2)) ? (kResidueBase - activeQ_) : 0;
        activeUseInt10_ = CanUseInt10Fold(activeFold28_);
        activeUseInt10TwoWrap_ = CanSkipInt10FirstWrap(activeFold28_);
        activeUseInt10OneWrap_ = CanSkipInt10SecondWrap(activeFold28_);
        activeTwiddlePhase12Base_ = twiddlePhase12Base_ + static_cast<size_t>(channel) * TwiddleCountPerChannel();
    }

    __aicore__ inline bool UseParallelDirectBat() const
    {
        return tiling_.n == 256 && tiling_.phase1KernelN == 256 &&
               tiling_.rnsChannels == 1 && tiling_.channelBlock == 1 &&
               tiling_.blockDim == kBatTileCount &&
               tiling_.subBench == 16 && tiling_.polyCount > 0 &&
               (activeQ_ == 3329 || activeQ_ == 8380417);
    }

    __aicore__ inline int32_t ParallelBatPaddedPolyCount() const
    {
        return AlignUpInt(tiling_.polyCount, tiling_.subBench);
    }

    __aicore__ inline size_t ParallelBatSplitPlaneBytes() const
    {
        return static_cast<size_t>(ParallelBatPaddedPolyCount()) *
               static_cast<size_t>(tiling_.n) * sizeof(int8_t);
    }

    __aicore__ inline size_t ParallelBatPartialMatrixBytes() const
    {
        return static_cast<size_t>(ParallelBatPaddedPolyCount()) *
               static_cast<size_t>(tiling_.n) * sizeof(int32_t);
    }

    __aicore__ inline GM_ADDR ParallelBatSplitPlane(int32_t digit) const
    {
        return scratchBase_ +
               static_cast<size_t>(digit) * ParallelBatSplitPlaneBytes();
    }

    __aicore__ inline GM_ADDR ParallelBatPartialMatrix(int32_t matrixId) const
    {
        return scratchBase_ +
               static_cast<size_t>(kDigitCount) *
                   ParallelBatSplitPlaneBytes() +
               static_cast<size_t>(matrixId) *
                   ParallelBatPartialMatrixBytes();
    }

    __aicore__ inline void ProcessParallelDirectBat(
        __gm__ int32_t* srcBase, __gm__ int32_t* dstBase,
        GM_ADDR matrixBase)
    {
        if (activeQ_ == 3329) {
            ProcessKemTilePipeline(srcBase, dstBase, matrixBase);
            return;
        }
        if (activeQ_ == 8380417 && ParallelPqcRoundCount() > 1) {
            ProcessDsaTilePipeline(srcBase, dstBase, matrixBase);
            return;
        }

        if (!isAic_) {
            ParallelBatSplit(srcBase);
        }
        AscendC::PipeBarrier<PIPE_ALL>();
        SyncAllCores();

        if (isAic_) {
            ParallelBatMmad(matrixBase);
        }
        AscendC::PipeBarrier<PIPE_ALL>();
        SyncAllCores();

        if (!isAic_) {
            ParallelBatMerge(dstBase);
        }
    }

    __aicore__ inline int32_t ParallelPqcTileCount() const
    {
        return CeilDivInt(
            ParallelBatPaddedPolyCount(), kParallelAivPolyBatch);
    }

    __aicore__ inline int32_t ParallelPqcRoundCount() const
    {
        return CeilDivInt(ParallelPqcTileCount(), logicalBlockCount_);
    }

    __aicore__ inline uint16_t PqcSplitState(int32_t slot) const
    {
        return static_cast<uint16_t>(2 * slot);
    }

    __aicore__ inline uint16_t PqcMergeState(int32_t slot) const
    {
        return static_cast<uint16_t>(2 * slot + 1);
    }

    __aicore__ inline void ProcessKemTilePipeline(
        __gm__ int32_t* srcBase, __gm__ int32_t* dstBase,
        GM_ADDR matrixBase)
    {
        constexpr int32_t pipelineDepth = 2;
        const int32_t roundCount = ParallelPqcRoundCount();
        if (isAic_) {
            for (int32_t round = 0; round < roundCount; ++round) {
                const int32_t tileId =
                    round * logicalBlockCount_ + logicalBlockId_;
                if (tileId >= ParallelPqcTileCount()) {
                    continue;
                }
                const int32_t slot = round % pipelineDepth;
                __WAIT(PqcSplitState(slot));
                ParallelKemParityMmad(matrixBase, tileId);
                __SET(PqcMergeState(slot), true);
            }
            return;
        }

        const int32_t prefill = MinInt(pipelineDepth, roundCount);
        for (int32_t round = 0; round < prefill; ++round) {
            const int32_t tileId =
                round * logicalBlockCount_ + logicalBlockId_;
            if (tileId >= ParallelPqcTileCount()) {
                continue;
            }
            const int32_t slot = round % pipelineDepth;
            ParallelKemParitySplit(srcBase, tileId);
            __SET(PqcSplitState(slot), false);
        }

        for (int32_t round = 0; round < roundCount; ++round) {
            const int32_t tileId =
                round * logicalBlockCount_ + logicalBlockId_;
            if (tileId >= ParallelPqcTileCount()) {
                continue;
            }
            const int32_t slot = round % pipelineDepth;
            __WAIT(PqcMergeState(slot));
            ParallelKemParityMerge(dstBase, tileId);

            const int32_t nextRound = round + pipelineDepth;
            const int32_t nextTile =
                nextRound * logicalBlockCount_ + logicalBlockId_;
            if (nextRound < roundCount &&
                nextTile < ParallelPqcTileCount()) {
                const int32_t nextSlot = nextRound % pipelineDepth;
                ParallelKemParitySplit(srcBase, nextTile);
                __SET(PqcSplitState(nextSlot), false);
            }
        }
    }

    __aicore__ inline void ProcessDsaTilePipeline(
        __gm__ int32_t* srcBase, __gm__ int32_t* dstBase,
        GM_ADDR matrixBase)
    {
        constexpr int32_t pipelineDepth = 2;
        const int32_t roundCount = ParallelPqcRoundCount();
        if (isAic_) {
            for (int32_t round = 0; round < roundCount; ++round) {
                const int32_t tileId =
                    round * logicalBlockCount_ + logicalBlockId_;
                if (tileId >= ParallelPqcTileCount()) {
                    continue;
                }
                const int32_t slot = round % pipelineDepth;
                __WAIT(PqcSplitState(slot));
                ParallelDsaTileMmad(matrixBase, tileId);
                __SET(PqcMergeState(slot), true);
            }
            return;
        }

        const int32_t prefill = MinInt(pipelineDepth, roundCount);
        for (int32_t round = 0; round < prefill; ++round) {
            const int32_t tileId =
                round * logicalBlockCount_ + logicalBlockId_;
            if (tileId >= ParallelPqcTileCount()) {
                continue;
            }
            const int32_t slot = round % pipelineDepth;
            ParallelBatSplit(srcBase, tileId);
            __SET(PqcSplitState(slot), false);
        }

        for (int32_t round = 0; round < roundCount; ++round) {
            const int32_t tileId =
                round * logicalBlockCount_ + logicalBlockId_;
            if (tileId >= ParallelPqcTileCount()) {
                continue;
            }
            const int32_t slot = round % pipelineDepth;
            __WAIT(PqcMergeState(slot));
            ParallelBatMerge(dstBase, tileId);
            const int32_t nextRound = round + pipelineDepth;
            const int32_t nextTile =
                nextRound * logicalBlockCount_ + logicalBlockId_;
            if (nextRound < roundCount &&
                nextTile < ParallelPqcTileCount()) {
                const int32_t nextSlot = nextRound % pipelineDepth;
                ParallelBatSplit(srcBase, nextTile);
                __SET(PqcSplitState(nextSlot), false);
            }
        }
    }

    __aicore__ inline void ParallelKemParitySplit(
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

        AscendC::TPipe pipe;
        AscendC::TQue<AscendC::TPosition::VECIN, 1> inSrc;
        AscendC::TBuf<AscendC::TPosition::VECCALC> digit0Buf,
            digit1Buf;
        AscendC::TBuf<AscendC::TPosition::VECCALC> tmp0Buf, tmp1Buf;
        pipe.InitBuffer(inSrc, 1, parityLength * sizeof(int32_t));
        pipe.InitBuffer(digit0Buf, parityLength * sizeof(int8_t));
        pipe.InitBuffer(digit1Buf, parityLength * sizeof(int8_t));
        pipe.InitBuffer(tmp0Buf, parityLength * sizeof(int32_t));
        pipe.InitBuffer(tmp1Buf, parityLength * sizeof(half));

        const size_t tileOffset = static_cast<size_t>(polyStart) * n;
        AscendC::DataCopyParams copyOut{
            static_cast<uint16_t>(tileRows),
            static_cast<uint16_t>(halfN * sizeof(int8_t) / 32), 0,
            static_cast<uint16_t>(halfN * sizeof(int8_t) / 32)};
        LocalTensor<int8_t> digit0 = digit0Buf.Get<int8_t>();
        LocalTensor<int8_t> digit1 = digit1Buf.Get<int8_t>();
        LocalTensor<int32_t> tmp0 = tmp0Buf.Get<int32_t>();
        LocalTensor<half> tmp1 = tmp1Buf.Get<half>();

        LocalTensor<int32_t> evenInput = inSrc.AllocTensor<int32_t>();
        if (validRows < tileRows) {
            AscendC::Duplicate(
                evenInput, static_cast<int32_t>(0), parityLength);
        }
        AscendC::GlobalTensor<int32_t> evenSrcGm;
        if (subBlockId_ == 0) {
            evenSrcGm.SetGlobalBuffer(
                srcBase + static_cast<size_t>(polyStart) * halfN,
                static_cast<size_t>(validRows) * halfN);
        } else {
            evenSrcGm.SetGlobalBuffer(
                srcBase + static_cast<size_t>(tiling_.polyCount) * halfN +
                    static_cast<size_t>(polyStart) * halfN,
                static_cast<size_t>(validRows) * halfN);
        }
        AscendC::DataCopy(evenInput, evenSrcGm, validRows * halfN);
        inSrc.EnQue(evenInput);
        LocalTensor<int32_t> even = inSrc.DeQue<int32_t>();
        Tensor_int8x2 evenSplit{digit0, digit1};
        split_vec_int7x2(
            evenSplit, even, tmp0, tmp1, parityLength);
        AscendC::PipeBarrier<PIPE_ALL>();
        AscendC::GlobalTensor<int8_t> evenPlane;
        if (subBlockId_ == 0) {
            evenPlane.SetGlobalBuffer(
                reinterpret_cast<__gm__ int8_t*>(
                    ParallelBatSplitPlane(0)) + tileOffset,
                tileRows * n);
        } else {
            evenPlane.SetGlobalBuffer(
                reinterpret_cast<__gm__ int8_t*>(
                    ParallelBatSplitPlane(1)) + tileOffset,
                tileRows * n);
        }
        AscendC::DataCopy(evenPlane, digit0, copyOut);
        AscendC::DataCopy(evenPlane[halfN], digit1, copyOut);
        AscendC::PipeBarrier<PIPE_ALL>();
        inSrc.FreeTensor(even);
    }

    __aicore__ inline void ParallelKemParityMmad(
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

    __aicore__ inline void ParallelDsaTileMmad(
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

    __aicore__ inline void ParallelKemParityMerge(
        __gm__ int32_t* dstBase, int32_t tileId)
    {
        constexpr int32_t n = 256;
        constexpr int32_t halfN = n / 2;
        constexpr int32_t tileRows = kParallelAivPolyBatch;
        constexpr int32_t halfLength = tileRows * halfN;
        const int32_t polyStart = tileId * tileRows;
        const int32_t validRows = polyStart < tiling_.polyCount
            ? MinInt(tileRows, tiling_.polyCount - polyStart)
            : 0;
        if (validRows <= 0) {
            return;
        }

        AscendC::TPipe pipe;
        AscendC::TBuf<AscendC::TPosition::VECCALC> digit0Buf,
            digit1Buf, resultBuf, tmp0Buf, tmp1Buf;
        pipe.InitBuffer(digit0Buf, halfLength * sizeof(int32_t));
        pipe.InitBuffer(digit1Buf, halfLength * sizeof(int32_t));
        pipe.InitBuffer(resultBuf, halfLength * sizeof(int32_t));
        pipe.InitBuffer(tmp0Buf, halfLength * sizeof(int32_t));
        pipe.InitBuffer(tmp1Buf, halfLength * sizeof(int32_t));
        LocalTensor<int32_t> digit0 = digit0Buf.Get<int32_t>();
        LocalTensor<int32_t> digit1 = digit1Buf.Get<int32_t>();
        LocalTensor<int32_t> result = resultBuf.Get<int32_t>();
        LocalTensor<int32_t> tmp0 = tmp0Buf.Get<int32_t>();
        LocalTensor<int32_t> tmp1 = tmp1Buf.Get<int32_t>();

        const size_t tileOffset =
            static_cast<size_t>(polyStart) * halfN;
        const int32_t matrixId = 2 * subBlockId_;
        AscendC::GlobalTensor<int32_t> partialGm;
        partialGm.SetGlobalBuffer(
            reinterpret_cast<__gm__ int32_t*>(
                ParallelBatPartialMatrix(matrixId)) + tileOffset,
            halfLength);
        AscendC::DataCopy(digit0, partialGm, halfLength);
        partialGm.SetGlobalBuffer(
            reinterpret_cast<__gm__ int32_t*>(
                ParallelBatPartialMatrix(matrixId + 1)) + tileOffset,
            halfLength);
        AscendC::DataCopy(digit1, partialGm, halfLength);
        AscendC::PipeBarrier<PIPE_ALL>();
        AscendC::Muls(result, digit1, 1, halfLength);
        ParallelBatReduce(result, tmp0, tmp1, halfLength, false);
        ParallelBatMergeStep(
            result, digit0, tmp0, tmp1, halfLength, false);
        AscendC::PipeBarrier<PIPE_ALL>();

        AscendC::GlobalTensor<int32_t> dst;
        dst.SetGlobalBuffer(
            dstBase +
                static_cast<size_t>(subBlockId_) * tiling_.polyCount * halfN +
                static_cast<size_t>(polyStart) * halfN,
            static_cast<size_t>(validRows) * halfN);
        AscendC::DataCopy(dst, result, validRows * halfN);
        AscendC::PipeBarrier<PIPE_ALL>();
    }

    __aicore__ inline void ParallelBatSplit(
        __gm__ int32_t* srcBase, int32_t forcedTile = -1)
    {
        const int32_t workerId =
            logicalBlockId_ * kMixAivPerAic + subBlockId_;
        const int32_t workerCount =
            logicalBlockCount_ * kMixAivPerAic;
        const int32_t paddedPolyCount = ParallelBatPaddedPolyCount();
        const int32_t n = tiling_.n;
        const int32_t tileCount =
            CeilDivInt(paddedPolyCount, kParallelAivPolyBatch);
        const int32_t panelsPerTile = forcedTile >= 0
            ? kMixAivPerAic
            : MinInt(8, workerCount / tileCount);
        const int32_t tileId =
            forcedTile >= 0 ? forcedTile : workerId / panelsPerTile;
        const int32_t panelId = forcedTile >= 0
            ? subBlockId_
            : workerId - tileId * panelsPerTile;
        if (tileId >= tileCount) {
            return;
        }
        const int32_t panelWidth = n / panelsPerTile;
        const int32_t coeffStart = panelId * panelWidth;
        const int32_t polyStart = tileId * kParallelAivPolyBatch;
        const int32_t validRows = polyStart < tiling_.polyCount
            ? MinInt(kParallelAivPolyBatch, tiling_.polyCount - polyStart)
            : 0;
        const int32_t tileLength = panelWidth * kParallelAivPolyBatch;

        AscendC::TPipe pipe;
        AscendC::TQue<AscendC::TPosition::VECIN, 1> inSrc;
        AscendC::TQue<AscendC::TPosition::VECOUT, 1> out0, out1, out2,
            out3;
        AscendC::TBuf<AscendC::TPosition::VECCALC> tmp0, tmp1, tmp2;
        pipe.InitBuffer(inSrc, 1, tileLength * sizeof(int32_t));
        pipe.InitBuffer(out0, 1, tileLength * sizeof(int8_t));
        pipe.InitBuffer(out1, 1, tileLength * sizeof(int8_t));
        pipe.InitBuffer(out2, 1, tileLength * sizeof(int8_t));
        pipe.InitBuffer(out3, 1, tileLength * sizeof(int8_t));
        pipe.InitBuffer(tmp0, tileLength * sizeof(int32_t));
        pipe.InitBuffer(tmp1, tileLength * sizeof(half));
        pipe.InitBuffer(tmp2, tileLength * sizeof(int32_t));

        LocalTensor<int32_t> localSrc = inSrc.AllocTensor<int32_t>();
        if (validRows < kParallelAivPolyBatch) {
            AscendC::Duplicate(
                localSrc, static_cast<int32_t>(0), tileLength);
        }
        if (validRows > 0) {
            AscendC::GlobalTensor<int32_t> srcGm;
            srcGm.SetGlobalBuffer(
                srcBase + static_cast<size_t>(polyStart) * n + coeffStart,
                static_cast<size_t>(validRows) * n);
            AscendC::DataCopyParams copyIn{
                static_cast<uint16_t>(validRows),
                static_cast<uint16_t>(panelWidth * sizeof(int32_t) / 32),
                static_cast<uint16_t>((n - panelWidth) * sizeof(int32_t) / 32),
                0};
            AscendC::DataCopy(localSrc, srcGm, copyIn);
        }
        inSrc.EnQue(localSrc);

        LocalTensor<int32_t> src = inSrc.DeQue<int32_t>();
        LocalTensor<int8_t> digit0 = out0.AllocTensor<int8_t>();
        LocalTensor<int8_t> digit1 = out1.AllocTensor<int8_t>();
        LocalTensor<int32_t> t0 = tmp0.Get<int32_t>();
        LocalTensor<half> t1 = tmp1.Get<half>();
        LocalTensor<int32_t> t2 = tmp2.Get<int32_t>();
        if (activeQ_ == 3329) {
            Tensor_int8x2 digits{digit0, digit1};
            split_vec_int7x2(digits, src, t0, t1, tileLength);
        } else {
            LocalTensor<int8_t> digit2 = out2.AllocTensor<int8_t>();
            Tensor_int8x3 digits{digit0, digit1, digit2};
            split_vec_int8x3_balanced(
                digits, src, t0, t2, t1, tileLength);
            out2.EnQue(digit2);
        }
        out0.EnQue(digit0);
        out1.EnQue(digit1);
        inSrc.FreeTensor(src);

        LocalTensor<int8_t> result0 = out0.DeQue<int8_t>();
        LocalTensor<int8_t> result1 = out1.DeQue<int8_t>();
        const size_t polyOffset = static_cast<size_t>(polyStart) * n;
        AscendC::GlobalTensor<int8_t> dst0, dst1;
        dst0.SetGlobalBuffer(
            reinterpret_cast<__gm__ int8_t*>(ParallelBatSplitPlane(0)) +
                polyOffset + coeffStart,
            static_cast<size_t>(kParallelAivPolyBatch) * n);
        dst1.SetGlobalBuffer(
            reinterpret_cast<__gm__ int8_t*>(ParallelBatSplitPlane(1)) +
                polyOffset + coeffStart,
            static_cast<size_t>(kParallelAivPolyBatch) * n);
        AscendC::DataCopyParams copyOut{
            static_cast<uint16_t>(kParallelAivPolyBatch),
            static_cast<uint16_t>(panelWidth * sizeof(int8_t) / 32), 0,
            static_cast<uint16_t>((n - panelWidth) * sizeof(int8_t) / 32)};
        AscendC::DataCopy(dst0, result0, copyOut);
        AscendC::DataCopy(dst1, result1, copyOut);
        if (activeQ_ != 3329) {
            LocalTensor<int8_t> result2 = out2.DeQue<int8_t>();
            AscendC::GlobalTensor<int8_t> dst2;
            dst2.SetGlobalBuffer(
                reinterpret_cast<__gm__ int8_t*>(ParallelBatSplitPlane(2)) +
                    polyOffset + coeffStart,
                static_cast<size_t>(kParallelAivPolyBatch) * n);
            AscendC::DataCopy(dst2, result2, copyOut);
            out2.FreeTensor(result2);
        }
        AscendC::PipeBarrier<PIPE_ALL>();
        out0.FreeTensor(result0);
        out1.FreeTensor(result1);
    }

    __aicore__ inline void ParallelBatMmad(GM_ADDR matrixBase)
    {
        const bool useKem2Digit = activeQ_ == 3329;
        const int32_t n = tiling_.n;
        const int32_t paddedPolyCount = ParallelBatPaddedPolyCount();
        const int32_t tileCount =
            CeilDivInt(paddedPolyCount, tiling_.subBench);
        const size_t matrixBytes = static_cast<size_t>(n) * n;
        AicMmad mm(tiling_.subBench, n, n);
        mm.Init();

        if (useKem2Digit) {
            const int32_t matrixOwner = logicalBlockId_ % 4;
            const int32_t matrixId =
                matrixOwner < 2 ? matrixOwner : matrixOwner + 2;
            const int32_t inDigit = matrixId % kDigitCount;
            mm.LoadB(
                matrixBase + static_cast<size_t>(matrixId) * matrixBytes);
            for (int32_t tile = logicalBlockId_ / 4;
                 tile < tileCount; tile += 4) {
                const size_t tilePolyOffset =
                    static_cast<size_t>(tile) * tiling_.subBench * n;
                mm.LoadA(
                    ParallelBatSplitPlane(inDigit) + tilePolyOffset);
                LocalTensor<int32_t> partial = mm.AllocAccum();
                mm.ComputeAccum(partial, true);
                mm.EnqueAccum(partial);
                mm.FreeA();
                mm.CopyOutQueued(
                    ParallelBatPartialMatrix(matrixId) +
                    tilePolyOffset * sizeof(int32_t));
            }
            mm.FreeB();
            return;
        }

        // ML-DSA uses nine compact [out_digit][in_digit] matrices.  Split the
        // full matrix/tile task space evenly so all 16 AICs execute nine MMADs
        // at batch=256; a core reloads B only when its interval crosses a
        // matrix boundary.
        constexpr int32_t kDsaMatrixCount = 9;
        const int32_t totalTasks = kDsaMatrixCount * tileCount;
        int32_t task = totalTasks * logicalBlockId_ / logicalBlockCount_;
        const int32_t taskEnd =
            totalTasks * (logicalBlockId_ + 1) / logicalBlockCount_;
        while (task < taskEnd) {
            const int32_t matrixId = task / tileCount;
            const int32_t matrixTaskEnd = MinInt(
                taskEnd, (matrixId + 1) * tileCount);
            mm.LoadB(
                matrixBase + static_cast<size_t>(matrixId) * matrixBytes);
            for (; task < matrixTaskEnd; ++task) {
                const int32_t tile = task - matrixId * tileCount;
                const size_t tilePolyOffset =
                    static_cast<size_t>(tile) * tiling_.subBench * n;
                mm.LoadA(
                    ParallelBatSplitPlane(matrixId % 3) + tilePolyOffset);
                LocalTensor<int32_t> partial = mm.AllocAccum();
                mm.ComputeAccum(partial, true);
                mm.EnqueAccum(partial);
                mm.FreeA();
                mm.CopyOutQueued(
                    ParallelBatPartialMatrix(matrixId) +
                    tilePolyOffset * sizeof(int32_t));
            }
            mm.FreeB();
        }
    }

    __aicore__ inline void ParallelBatLoadDigitSum(
        LocalTensor<int32_t>& dst, LocalTensor<int32_t>& tmp,
        int32_t outDigit, int32_t polyStart, int32_t coeffStart,
        int32_t panelWidth, int32_t n)
    {
        const size_t polyOffset = static_cast<size_t>(polyStart) * n;
        const int32_t count = kParallelAivPolyBatch * panelWidth;
        AscendC::DataCopyParams copyIn{
            static_cast<uint16_t>(kParallelAivPolyBatch),
            static_cast<uint16_t>(panelWidth * sizeof(int32_t) / 32),
            static_cast<uint16_t>((n - panelWidth) * sizeof(int32_t) / 32),
            0};
        const int32_t matrixDigitCount = activeQ_ == 3329 ? 4 : 3;
        AscendC::GlobalTensor<int32_t> src;
        src.SetGlobalBuffer(
            reinterpret_cast<__gm__ int32_t*>(
                ParallelBatPartialMatrix(
                    outDigit * matrixDigitCount)) +
                polyOffset + coeffStart,
            static_cast<size_t>(kParallelAivPolyBatch) * n);
        AscendC::DataCopy(dst, src, copyIn);
        AscendC::PipeBarrier<PIPE_ALL>();

        const int32_t inputDigitCount = activeQ_ == 3329 ? 2 : 3;
        for (int32_t inDigit = 1; inDigit < inputDigitCount; ++inDigit) {
            src.SetGlobalBuffer(
                reinterpret_cast<__gm__ int32_t*>(
                    ParallelBatPartialMatrix(
                        outDigit * matrixDigitCount + inDigit)) +
                    polyOffset + coeffStart,
                static_cast<size_t>(kParallelAivPolyBatch) * n);
            AscendC::DataCopy(tmp, src, copyIn);
            AscendC::PipeBarrier<PIPE_ALL>();
            AscendC::Add(dst, dst, tmp, count);
            AscendC::PipeBarrier<PIPE_ALL>();
        }
    }

    __aicore__ inline void ParallelBatReduce(
        LocalTensor<int32_t>& value, LocalTensor<int32_t>& tmp0,
        LocalTensor<int32_t>& tmp1, int32_t count, bool fastFold)
    {
        if (fastFold) {
            wrap_mod_vec_runtime(value, value, activeQ_, tmp0, tmp1, count);
            return;
        }
        barrett_mul_vec_runtime(
            value, activeQ_, activeBarrettK_,
            static_cast<int32_t>(activeBarrettMu_), tmp0, tmp1, count);
    }

    __aicore__ inline void ParallelBatReduceSignedDsa(
        LocalTensor<int32_t>& value, LocalTensor<int32_t>& tmp0,
        LocalTensor<int32_t>& tmp1, int32_t count)
    {
        // A 3x256xint8 dot product is in [-12.5M, 12.6M].  Biasing by
        // 2*q makes the existing Barrett reduction valid for signed sums.
        AscendC::Adds(value, value, 2 * 8380417, count);
        barrett_mul_vec_runtime(
            value, 8380417, activeBarrettK_,
            static_cast<int32_t>(activeBarrettMu_), tmp0, tmp1, count);
    }

    __aicore__ inline void ParallelBatMulBase256Dsa(
        LocalTensor<int32_t>& value, LocalTensor<int32_t>& tmp0,
        LocalTensor<int32_t>& tmp1, int32_t count)
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

    __aicore__ inline void ParallelBatMergeStepDsa(
        LocalTensor<int32_t>& value, LocalTensor<int32_t>& addend,
        LocalTensor<int32_t>& tmp0, LocalTensor<int32_t>& tmp1,
        int32_t count)
    {
        ParallelBatMulBase256Dsa(value, tmp0, tmp1, count);
        ParallelBatReduceSignedDsa(addend, tmp0, tmp1, count);
        AscendC::Add(value, value, addend, count);
        wrap_mod_vec_runtime(value, value, 8380417, tmp0, tmp1, count);
    }

    __aicore__ inline void ParallelBatMergeStep(
        LocalTensor<int32_t>& value, LocalTensor<int32_t>& addend,
        LocalTensor<int32_t>& tmp0, LocalTensor<int32_t>& tmp1,
        int32_t count, bool fastFold)
    {
        if (activeQ_ == 3329 || activeQ_ == 8380417) {
            AscendC::Muls(value, value, kDigitBase, count);
            AscendC::Add(value, value, addend, count);
            ParallelBatReduce(value, tmp0, tmp1, count, false);
            return;
        }
        if (fastFold) {
            MulBase7FoldNoWrapVec(
                value, activeFold28_, tmp0, tmp1, count);
            AscendC::Add(value, value, addend, count);
            wrap_mod_vec_runtime(
                value, value, activeQ_, tmp0, tmp1, count);
            return;
        }
        MulBaseModVecRuntime(
            value, activeQ_, activeFold28_, tmp0, tmp1, count);
        AscendC::Add(value, value, addend, count);
        ParallelBatReduce(value, tmp0, tmp1, count, fastFold);
    }

    __aicore__ inline void ParallelBatMerge(
        __gm__ int32_t* dstBase, int32_t forcedTile = -1)
    {
        const int32_t workerId =
            logicalBlockId_ * kMixAivPerAic + subBlockId_;
        const int32_t workerCount =
            logicalBlockCount_ * kMixAivPerAic;
        const int32_t n = tiling_.n;
        const bool fastFold = CanUseBase7FoldWithAdd(activeFold28_, n);
        const int32_t paddedPolyCount = ParallelBatPaddedPolyCount();
        const int32_t tileCount =
            CeilDivInt(paddedPolyCount, kParallelAivPolyBatch);
        const int32_t panelsPerTile = forcedTile >= 0
            ? kMixAivPerAic
            : MinInt(8, workerCount / tileCount);
        const int32_t tileId =
            forcedTile >= 0 ? forcedTile : workerId / panelsPerTile;
        const int32_t panelId = forcedTile >= 0
            ? subBlockId_
            : workerId - tileId * panelsPerTile;
        if (tileId >= tileCount) {
            return;
        }
        const int32_t panelWidth = n / panelsPerTile;
        const int32_t coeffStart = panelId * panelWidth;
        const int32_t polyStart = tileId * kParallelAivPolyBatch;
        const int32_t validRows = polyStart < tiling_.polyCount
            ? MinInt(kParallelAivPolyBatch, tiling_.polyCount - polyStart)
            : 0;
        const int32_t tileLength = panelWidth * kParallelAivPolyBatch;

        AscendC::TPipe pipe;
        AscendC::TBuf<AscendC::TPosition::VECCALC> digit0Buf, digit1Buf,
            digit2Buf, resultBuf, partialBuf, tmp0Buf, tmp1Buf;
        pipe.InitBuffer(digit0Buf, tileLength * sizeof(int32_t));
        pipe.InitBuffer(digit1Buf, tileLength * sizeof(int32_t));
        pipe.InitBuffer(digit2Buf, tileLength * sizeof(int32_t));
        pipe.InitBuffer(resultBuf, tileLength * sizeof(int32_t));
        pipe.InitBuffer(partialBuf, tileLength * sizeof(int32_t));
        pipe.InitBuffer(tmp0Buf, tileLength * sizeof(int32_t));
        pipe.InitBuffer(tmp1Buf, tileLength * sizeof(int32_t));

        LocalTensor<int32_t> digit0 = digit0Buf.Get<int32_t>();
        LocalTensor<int32_t> digit1 = digit1Buf.Get<int32_t>();
        LocalTensor<int32_t> digit2 = digit2Buf.Get<int32_t>();
        LocalTensor<int32_t> result = resultBuf.Get<int32_t>();
        LocalTensor<int32_t> partial = partialBuf.Get<int32_t>();
        LocalTensor<int32_t> tmp0 = tmp0Buf.Get<int32_t>();
        LocalTensor<int32_t> tmp1 = tmp1Buf.Get<int32_t>();

        const int32_t count = kParallelAivPolyBatch * panelWidth;
        ParallelBatLoadDigitSum(
            digit0, partial, 0, polyStart, coeffStart, panelWidth, n);
        ParallelBatLoadDigitSum(
            digit1, partial, 1, polyStart, coeffStart, panelWidth, n);

        if (activeQ_ == 3329) {
            AscendC::Muls(result, digit1, 1, count);
            ParallelBatReduce(result, tmp0, tmp1, count, false);
            ParallelBatMergeStep(
                result, digit0, tmp0, tmp1, count, false);
        } else {
            ParallelBatLoadDigitSum(
                digit2, partial, 2, polyStart, coeffStart, panelWidth, n);
            AscendC::Muls(result, digit2, 1, count);
            ParallelBatReduceSignedDsa(result, tmp0, tmp1, count);
            ParallelBatMergeStepDsa(
                result, digit1, tmp0, tmp1, count);
            ParallelBatMergeStepDsa(
                result, digit0, tmp0, tmp1, count);
        }
        AscendC::PipeBarrier<PIPE_ALL>();

        if (validRows > 0) {
            AscendC::GlobalTensor<int32_t> dst;
            dst.SetGlobalBuffer(
                dstBase + static_cast<size_t>(polyStart) * n + coeffStart,
                static_cast<size_t>(validRows) * n);
            AscendC::DataCopyParams copyOut{
                static_cast<uint16_t>(validRows),
                static_cast<uint16_t>(panelWidth * sizeof(int32_t) / 32),
                0,
                static_cast<uint16_t>((n - panelWidth) * sizeof(int32_t) / 32)};
            AscendC::DataCopy(dst, result, copyOut);
            AscendC::PipeBarrier<PIPE_ALL>();
        }
    }

    __aicore__ inline void SubNTT(
        __gm__ int32_t* srcBase, int32_t srcStride, __gm__ int32_t* dstBase, int32_t dstStride, int32_t kernelN,
        int32_t logicalN, int32_t totalBench, GM_ADDR matrixBase)
    {
        const int32_t paddedBenchWidth = PaddedBenchInt(kernelN, tiling_.subBench);
        const int32_t roundSpan = logicalBlockCount_ * tiling_.subBench;
        const int32_t roundCount = CeilDivInt(totalBench, roundSpan);
        const size_t scratchPerBlock = ScratchBytesInt(kernelN, paddedBenchWidth);

        if (CanRunSubNttPipeline(roundCount)) {
            RunSubNTTPipeline(
                srcBase, srcStride, dstBase, dstStride, matrixBase, kernelN, logicalN, totalBench, paddedBenchWidth,
                scratchPerBlock, roundSpan, roundCount);
            return;
        }

        for (int32_t round = 0; round < roundCount; ++round) {
            RunSubNTTSerialTask(
                srcBase, srcStride, dstBase, dstStride, matrixBase, kernelN, logicalN, totalBench, paddedBenchWidth,
                scratchPerBlock, roundSpan, round);
        }
    }

    __aicore__ inline bool CanRunSubNttPipeline(int32_t roundCount)
    {
        return roundCount > 1 && static_cast<int32_t>(kPipelinedCrossCoreStates) <= kFirstSyncAllReservedFlag;
    }

    __aicore__ inline uint16_t SubNttStateId(int32_t slot, uint16_t statesPerBlock)
    {
        (void)statesPerBlock;
        return static_cast<uint16_t>(slot);
    }

    __aicore__ inline GM_ADDR ScratchSlotBase(size_t scratchPerBlock, int32_t slot)
    {
        const size_t logicalSlot = static_cast<size_t>(logicalBlockId_) * static_cast<size_t>(kSubNttPipelineSlots) +
                                   static_cast<size_t>(slot);
        return scratchBase_ + logicalSlot * scratchPerBlock;
    }

    __aicore__ inline void RoundBlockBench(
        int32_t round, int32_t roundSpan, int32_t totalBench, int32_t& blockBenchOffset, int32_t& actualBench)
    {
        blockBenchOffset = round * roundSpan + logicalBlockId_ * tiling_.subBench;
        const int32_t remaining = totalBench - blockBenchOffset;
        actualBench = remaining > 0 ? MinInt(tiling_.subBench, remaining) : 0;
    }

    __aicore__ inline bool RoundHasWork(int32_t round, int32_t roundSpan, int32_t totalBench)
    {
        return round * roundSpan + logicalBlockId_ * tiling_.subBench < totalBench;
    }

    __aicore__ inline void RunSubNTTSerialTask(
        __gm__ int32_t* srcBase, int32_t srcStride, __gm__ int32_t* dstBase, int32_t dstStride, GM_ADDR matrixBase,
        int32_t kernelN, int32_t logicalN, int32_t totalBench, int32_t paddedBenchWidth, size_t scratchPerBlock,
        int32_t roundSpan, int32_t round)
    {
        if (!RoundHasWork(round, roundSpan, totalBench)) {
            return;
        }

        constexpr int32_t slot = 0;
        const uint16_t stateId = SubNttStateId(slot, kSerialCrossCoreStates);
        if (isAic_) {
            __WAIT(stateId);
            RunSubNTTMmadTask(matrixBase, kernelN, paddedBenchWidth, scratchPerBlock, slot);
            __SET(stateId, isAic_);
            return;
        }

        RunSubNTTSplitTask(
            srcBase, srcStride, kernelN, logicalN, totalBench, paddedBenchWidth, scratchPerBlock, roundSpan, round,
            slot);
        __SET(stateId, isAic_);
        __WAIT(stateId);
        RunSubNTTMergeTask(
            dstBase, dstStride, kernelN, logicalN, totalBench, paddedBenchWidth, scratchPerBlock, roundSpan, round,
            slot);
    }

    __aicore__ inline void RunSubNTTPipeline(
        __gm__ int32_t* srcBase, int32_t srcStride, __gm__ int32_t* dstBase, int32_t dstStride, GM_ADDR matrixBase,
        int32_t kernelN, int32_t logicalN, int32_t totalBench, int32_t paddedBenchWidth, size_t scratchPerBlock,
        int32_t roundSpan, int32_t roundCount)
    {
        if (isAic_) {
            for (int32_t round = 0; round < roundCount; ++round) {
                if (!RoundHasWork(round, roundSpan, totalBench)) {
                    continue;
                }
                const int32_t slot = round % kSubNttPipelineSlots;
                const uint16_t stateId = SubNttStateId(slot, kPipelinedCrossCoreStates);
                __WAIT(stateId);
                RunSubNTTMmadTask(matrixBase, kernelN, paddedBenchWidth, scratchPerBlock, slot);
                __SET(stateId, isAic_);
            }
            return;
        }

        const int32_t prefillRounds = MinInt(kSubNttPipelineSlots, roundCount);
        for (int32_t prefillRound = 0; prefillRound < prefillRounds; ++prefillRound) {
            if (!RoundHasWork(prefillRound, roundSpan, totalBench)) {
                continue;
            }
            const int32_t slot = prefillRound % kSubNttPipelineSlots;
            RunSubNTTSplitTask(
                srcBase, srcStride, kernelN, logicalN, totalBench, paddedBenchWidth, scratchPerBlock, roundSpan,
                prefillRound, slot);
            __SET(SubNttStateId(slot, kPipelinedCrossCoreStates), isAic_);
        }

        for (int32_t round = 0; round < roundCount; ++round) {
            if (!RoundHasWork(round, roundSpan, totalBench)) {
                continue;
            }

            const int32_t slot = round % kSubNttPipelineSlots;
            const uint16_t stateId = SubNttStateId(slot, kPipelinedCrossCoreStates);
            __WAIT(stateId);
            RunSubNTTMergeTask(
                dstBase, dstStride, kernelN, logicalN, totalBench, paddedBenchWidth, scratchPerBlock, roundSpan, round,
                slot);

            const int32_t nextRound = round + kSubNttPipelineSlots;
            if (nextRound < roundCount && RoundHasWork(nextRound, roundSpan, totalBench)) {
                const int32_t nextSlot = nextRound % kSubNttPipelineSlots;
                RunSubNTTSplitTask(
                    srcBase, srcStride, kernelN, logicalN, totalBench, paddedBenchWidth, scratchPerBlock, roundSpan,
                    nextRound, nextSlot);
                __SET(SubNttStateId(nextSlot, kPipelinedCrossCoreStates), isAic_);
            }
        }
    }

    __aicore__ inline void RunSubNTTMmadTask(
        GM_ADDR matrixBase, int32_t kernelN, int32_t paddedBenchWidth, size_t scratchPerBlock, int32_t slot)
    {
        const size_t matrixTileBytes = static_cast<size_t>(kernelN) * kernelN;
        const size_t splitTileBytes = static_cast<size_t>(kernelN) * paddedBenchWidth * sizeof(int8_t);
        const size_t accumSliceBytes = static_cast<size_t>(kernelN) * paddedBenchWidth * sizeof(int32_t);
        GM_ADDR scratchBlock = ScratchSlotBase(scratchPerBlock, slot);

        const size_t S0 = 0;
        const size_t S1 = S0 + splitTileBytes;
        const size_t S2 = S1 + splitTileBytes;
        const size_t S3 = S2 + splitTileBytes;
        const size_t A0 = S3 + splitTileBytes;
        const size_t A1 = A0 + accumSliceBytes;
        const size_t A2 = A1 + accumSliceBytes;
        const size_t A3 = A2 + accumSliceBytes;

        AicMmad mmol(paddedBenchWidth, kernelN, kernelN);
        mmol.Init();

        AscendC::LocalTensor<int32_t> accum0 = mmol.AllocAccum();
        AscendC::LocalTensor<int32_t> accum1 = mmol.AllocAccum();
        AscendC::LocalTensor<int32_t> accum2 = mmol.AllocAccum();
        AscendC::LocalTensor<int32_t> accum3 = mmol.AllocAccum();

        for (int32_t inChunk = 0; inChunk < kDigitCount; ++inChunk) {
            GM_ADDR aPtr = scratchBlock + (inChunk == 0 ? S0 : inChunk == 1 ? S1 : inChunk == 2 ? S2 : S3);
            mmol.LoadA(aPtr);

            GM_ADDR bPtr0 = matrixBase + (static_cast<size_t>(0 * kDigitCount + inChunk) * matrixTileBytes);
            mmol.LoadB(bPtr0);
            mmol.ComputeAccum(accum0, inChunk == 0);
            mmol.FreeB();

            GM_ADDR bPtr1 = matrixBase + (static_cast<size_t>(1 * kDigitCount + inChunk) * matrixTileBytes);
            mmol.LoadB(bPtr1);
            mmol.ComputeAccum(accum1, inChunk == 0);
            mmol.FreeB();

            GM_ADDR bPtr2 = matrixBase + (static_cast<size_t>(2 * kDigitCount + inChunk) * matrixTileBytes);
            mmol.LoadB(bPtr2);
            mmol.ComputeAccum(accum2, inChunk == 0);
            mmol.FreeB();

            GM_ADDR bPtr3 = matrixBase + (static_cast<size_t>(3 * kDigitCount + inChunk) * matrixTileBytes);
            mmol.LoadB(bPtr3);
            mmol.ComputeAccum(accum3, inChunk == 0);
            mmol.FreeB();

            mmol.FreeA();
        }

        mmol.EnqueAccum(accum0);
        mmol.EnqueAccum(accum1);
        mmol.EnqueAccum(accum2);
        mmol.EnqueAccum(accum3);

        mmol.CopyOutQueued(scratchBlock + A0);
        mmol.CopyOutQueued(scratchBlock + A1);
        mmol.CopyOutQueued(scratchBlock + A2);
        mmol.CopyOutQueued(scratchBlock + A3);
    }

    __aicore__ inline void RunSubNTTSplitTask(
        __gm__ int32_t* srcBase, int32_t srcStride, int32_t kernelN, int32_t logicalN, int32_t totalBench,
        int32_t paddedBenchWidth, size_t scratchPerBlock, int32_t roundSpan, int32_t round, int32_t slot)
    {
        int32_t blockBenchOffset = 0;
        int32_t actualBench = 0;
        RoundBlockBench(round, roundSpan, totalBench, blockBenchOffset, actualBench);
        const size_t splitTileBytes = static_cast<size_t>(kernelN) * paddedBenchWidth * sizeof(int8_t);
        GM_ADDR scratchBlock = ScratchSlotBase(scratchPerBlock, slot);
        const size_t S0 = 0;
        const size_t S1 = S0 + splitTileBytes;
        const size_t S2 = S1 + splitTileBytes;
        const size_t S3 = S2 + splitTileBytes;

        DirectSplitBlockHalf(
            scratchBlock + S0, scratchBlock + S1, scratchBlock + S2, scratchBlock + S3, srcBase, srcStride,
            blockBenchOffset, actualBench, logicalN, kernelN, paddedBenchWidth);
        AscendC::PipeBarrier<PIPE_ALL>();
    }

    __aicore__ inline void RunSubNTTMergeTask(
        __gm__ int32_t* dstBase, int32_t dstStride, int32_t kernelN, int32_t logicalN, int32_t totalBench,
        int32_t paddedBenchWidth, size_t scratchPerBlock, int32_t roundSpan, int32_t round, int32_t slot)
    {
        int32_t blockBenchOffset = 0;
        int32_t actualBench = 0;
        RoundBlockBench(round, roundSpan, totalBench, blockBenchOffset, actualBench);
        const size_t splitTileBytes = static_cast<size_t>(kernelN) * paddedBenchWidth * sizeof(int8_t);
        const size_t accumSliceBytes = static_cast<size_t>(kernelN) * paddedBenchWidth * sizeof(int32_t);
        GM_ADDR scratchBlock = ScratchSlotBase(scratchPerBlock, slot);
        const size_t S0 = 0;
        const size_t S1 = S0 + splitTileBytes;
        const size_t S2 = S1 + splitTileBytes;
        const size_t S3 = S2 + splitTileBytes;
        const size_t A0 = S3 + splitTileBytes;
        const size_t A1 = A0 + accumSliceBytes;
        const size_t A2 = A1 + accumSliceBytes;
        const size_t A3 = A2 + accumSliceBytes;

        BatAivCombine4 merge(
            subBlockId_, kernelN, paddedBenchWidth, activeQ_, activeBarrettK_, activeBarrettMu_, activeFold28_);
        merge.Init(
            dstBase, dstStride, blockBenchOffset, actualBench, logicalN, scratchBlock + A0, scratchBlock + A1,
            scratchBlock + A2, scratchBlock + A3);
        merge.CopyIn();
        merge.Compute();
        merge.CopyOut();
    }

    __aicore__ inline void DirectSplitBlockHalf(
        GM_ADDR dst0, GM_ADDR dst1, GM_ADDR dst2, GM_ADDR dst3, __gm__ int32_t* srcBase, int32_t srcStride,
        int32_t blockBenchOffset, int32_t actualBench, int32_t logicalN, int32_t kernelN, int32_t paddedBenchWidth)
    {
        const int32_t halfRows = paddedBenchWidth / kMixAivPerAic;
        const int32_t rowStart = subBlockId_ * halfRows;
        const int32_t tileLength = kernelN * halfRows;
        const int32_t validRows = actualBench > rowStart ? MinInt(halfRows, actualBench - rowStart) : 0;
        const bool denseBenchLayout = (srcStride == logicalN) && (logicalN == kernelN);

        AscendC::TPipe pipe;
        AscendC::TBuf<AscendC::TPosition::VECCALC> buf1, buf2;
        AscendC::TQue<AscendC::TPosition::VECIN, 1> inSrc;
        AscendC::TQue<AscendC::TPosition::VECOUT, 1> outDst0, outDst1, outDst2, outDst3;
        pipe.InitBuffer(inSrc, 1, static_cast<uint32_t>(tileLength * sizeof(int32_t)));
        pipe.InitBuffer(outDst0, 1, static_cast<uint32_t>(tileLength * sizeof(int8_t)));
        pipe.InitBuffer(outDst1, 1, static_cast<uint32_t>(tileLength * sizeof(int8_t)));
        pipe.InitBuffer(outDst2, 1, static_cast<uint32_t>(tileLength * sizeof(int8_t)));
        pipe.InitBuffer(outDst3, 1, static_cast<uint32_t>(tileLength * sizeof(int8_t)));
        pipe.InitBuffer(buf1, static_cast<uint32_t>(tileLength * sizeof(int32_t)));
        pipe.InitBuffer(buf2, static_cast<uint32_t>(tileLength * sizeof(half)));

        LocalTensor<int32_t> localSrc = inSrc.AllocTensor<int32_t>();
        if (validRows == halfRows && denseBenchLayout) {
            AscendC::GlobalTensor<int32_t> srcGm;
            srcGm.SetGlobalBuffer(
                srcBase + static_cast<size_t>(blockBenchOffset + rowStart) * srcStride,
                static_cast<size_t>(tileLength));
            AscendC::DataCopy(localSrc, srcGm, tileLength);
        } else {
            AscendC::Duplicate(localSrc, static_cast<int32_t>(0), tileLength);
            if (validRows > 0) {
                if (denseBenchLayout) {
                    AscendC::GlobalTensor<int32_t> srcGm;
                    srcGm.SetGlobalBuffer(
                        srcBase + static_cast<size_t>(blockBenchOffset + rowStart) * srcStride,
                        static_cast<size_t>(validRows) * kernelN);
                    AscendC::DataCopy(localSrc, srcGm, validRows * kernelN);
                } else {
                    for (int32_t localRow = 0; localRow < validRows; ++localRow) {
                        const int32_t row = rowStart + localRow;
                        AscendC::GlobalTensor<int32_t> srcGm;
                        srcGm.SetGlobalBuffer(
                            srcBase + static_cast<size_t>(blockBenchOffset + row) * srcStride, logicalN);
                        AscendC::DataCopy(localSrc[localRow * kernelN], srcGm, logicalN);
                    }
                }
            }
        }
        inSrc.EnQue(localSrc);

        LocalTensor<int32_t> srcTensor = inSrc.DeQue<int32_t>();
        LocalTensor<int8_t> localDst0 = outDst0.AllocTensor<int8_t>();
        LocalTensor<int8_t> localDst1 = outDst1.AllocTensor<int8_t>();
        LocalTensor<int8_t> localDst2 = outDst2.AllocTensor<int8_t>();
        LocalTensor<int8_t> localDst3 = outDst3.AllocTensor<int8_t>();
        LocalTensor<int32_t> t1 = buf1.Get<int32_t>();
        LocalTensor<half> t2 = buf2.Get<half>();
        Tensor_int8x4 res{localDst0, localDst1, localDst2, localDst3};
        split_vec_int7x4(res, srcTensor, t1, t2, tileLength);
        outDst0.EnQue(localDst0);
        outDst1.EnQue(localDst1);
        outDst2.EnQue(localDst2);
        outDst3.EnQue(localDst3);
        inSrc.FreeTensor(srcTensor);

        AscendC::GlobalTensor<int8_t> gmDst0;
        AscendC::GlobalTensor<int8_t> gmDst1;
        AscendC::GlobalTensor<int8_t> gmDst2;
        AscendC::GlobalTensor<int8_t> gmDst3;
        gmDst0.SetGlobalBuffer((__gm__ int8_t*)dst0, static_cast<size_t>(kernelN) * paddedBenchWidth);
        gmDst1.SetGlobalBuffer((__gm__ int8_t*)dst1, static_cast<size_t>(kernelN) * paddedBenchWidth);
        gmDst2.SetGlobalBuffer((__gm__ int8_t*)dst2, static_cast<size_t>(kernelN) * paddedBenchWidth);
        gmDst3.SetGlobalBuffer((__gm__ int8_t*)dst3, static_cast<size_t>(kernelN) * paddedBenchWidth);

        LocalTensor<int8_t> out0 = outDst0.DeQue<int8_t>();
        LocalTensor<int8_t> out1 = outDst1.DeQue<int8_t>();
        LocalTensor<int8_t> out2 = outDst2.DeQue<int8_t>();
        LocalTensor<int8_t> out3 = outDst3.DeQue<int8_t>();
        const int32_t gmOffset = subBlockId_ * tileLength;
        AscendC::DataCopy(gmDst0[gmOffset], out0, tileLength);
        AscendC::DataCopy(gmDst1[gmOffset], out1, tileLength);
        AscendC::DataCopy(gmDst2[gmOffset], out2, tileLength);
        AscendC::DataCopy(gmDst3[gmOffset], out3, tileLength);
        AscendC::PipeBarrier<PIPE_ALL>();
        outDst0.FreeTensor(out0);
        outDst1.FreeTensor(out1);
        outDst2.FreeTensor(out2);
        outDst3.FreeTensor(out3);
    }

    __aicore__ inline void PreparePhase2Input64x64RowPairInt10()
    {
        constexpr int32_t kFactor = 64;
        constexpr int32_t kPanel = 32;
        constexpr int32_t kRowPanels = kFactor / kPanel;
        constexpr int32_t kPanelElemCount = kPanel * kPanel;
        constexpr int32_t kHalfElemCount = kPanel * kFactor;

        const int32_t workerId = logicalBlockId_ * kMixAivPerAic + subBlockId_;
        const int32_t workerCount = aivCoreCount_;
        int32_t rowPanelIdx = -1;
        int32_t workerInRow = 0;
        int32_t workersForRow = 0;

        const int32_t baseWorkersPerRow = workerCount / kRowPanels;
        const int32_t extraWorkers = workerCount - baseWorkersPerRow * kRowPanels;
        int32_t workerCursor = 0;
        for (int32_t rowIdx = 0; rowIdx < kRowPanels; ++rowIdx) {
            const int32_t rowWorkers = baseWorkersPerRow + (rowIdx < extraWorkers ? 1 : 0);
            if (workerId >= workerCursor && workerId < workerCursor + rowWorkers) {
                rowPanelIdx = rowIdx;
                workerInRow = workerId - workerCursor;
                workersForRow = rowWorkers;
                break;
            }
            workerCursor += rowWorkers;
        }
        if (rowPanelIdx < 0 || workersForRow <= 0) {
            return;
        }

        const int32_t polysPerWorker = CeilDivInt(tiling_.polyCount, workersForRow);
        const int32_t polyStart = workerInRow * polysPerWorker;
        const int32_t polyEnd = MinInt(tiling_.polyCount, polyStart + polysPerWorker);
        if (polyStart >= polyEnd) {
            return;
        }

        AscendC::TPipe pipe;
        AscendC::TBuf<AscendC::TPosition::VECCALC> bufIn, bufInFull, bufTwiddle, bufMul, bufOut;
        AscendC::TBuf<AscendC::TPosition::VECCALC> bufTranspose, bufSelect0, bufSelect1;
        AscendC::TBuf<AscendC::TPosition::VECCALC> tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6;
        AscendC::TBuf<AscendC::TPosition::VECCALC> tmp7, tmp8, tmp9, tmp10, tmp11, tmp12;
        pipe.InitBuffer(bufIn, kPanelElemCount * sizeof(int32_t));
        pipe.InitBuffer(bufInFull, kHalfElemCount * sizeof(int32_t));
        pipe.InitBuffer(bufTwiddle, kPanelElemCount * sizeof(int32_t));
        pipe.InitBuffer(bufMul, kPanelElemCount * sizeof(int32_t));
        pipe.InitBuffer(bufOut, kPanelElemCount * sizeof(int32_t));
        pipe.InitBuffer(bufTranspose, kPanelElemCount * sizeof(uint32_t));
        pipe.InitBuffer(bufSelect0, kPanelElemCount * sizeof(uint32_t));
        pipe.InitBuffer(bufSelect1, kPanelElemCount * sizeof(uint32_t));
        pipe.InitBuffer(tmp0, kPanelElemCount * sizeof(int32_t));
        pipe.InitBuffer(tmp1, kPanelElemCount * sizeof(int32_t));
        pipe.InitBuffer(tmp2, kPanelElemCount * sizeof(int32_t));
        pipe.InitBuffer(tmp3, kPanelElemCount * sizeof(int32_t));
        pipe.InitBuffer(tmp4, kPanelElemCount * sizeof(int32_t));
        pipe.InitBuffer(tmp5, kPanelElemCount * sizeof(int32_t));
        pipe.InitBuffer(tmp6, kPanelElemCount * sizeof(int32_t));
        pipe.InitBuffer(tmp7, kPanelElemCount * sizeof(int32_t));
        pipe.InitBuffer(tmp8, kPanelElemCount * sizeof(int32_t));
        pipe.InitBuffer(tmp9, kPanelElemCount * sizeof(int32_t));
        pipe.InitBuffer(tmp10, kPanelElemCount * sizeof(int32_t));
        pipe.InitBuffer(tmp11, kPanelElemCount * sizeof(int32_t));
        pipe.InitBuffer(tmp12, kPanelElemCount * sizeof(int32_t));

        LocalTensor<int32_t> panelInBuf = bufIn.Get<int32_t>();
        LocalTensor<int32_t> panelInFullBuf = bufInFull.Get<int32_t>();
        LocalTensor<int32_t> panelTwiddleBuf = bufTwiddle.Get<int32_t>();
        LocalTensor<int32_t> panelMulBuf = bufMul.Get<int32_t>();
        LocalTensor<int32_t> panelOutBuf = bufOut.Get<int32_t>();
        LocalTensor<uint32_t> transposeLut = bufTranspose.Get<uint32_t>();
        LocalTensor<uint32_t> select0Lut = bufSelect0.Get<uint32_t>();
        LocalTensor<uint32_t> select1Lut = bufSelect1.Get<uint32_t>();
        LocalTensor<int32_t> t0 = tmp0.Get<int32_t>();
        LocalTensor<int32_t> t1 = tmp1.Get<int32_t>();
        LocalTensor<int32_t> t2 = tmp2.Get<int32_t>();
        LocalTensor<int32_t> t3 = tmp3.Get<int32_t>();
        LocalTensor<int32_t> t4 = tmp4.Get<int32_t>();
        LocalTensor<int32_t> t5 = tmp5.Get<int32_t>();
        LocalTensor<int32_t> t6 = tmp6.Get<int32_t>();
        LocalTensor<int32_t> t7 = tmp7.Get<int32_t>();
        LocalTensor<int32_t> t8 = tmp8.Get<int32_t>();
        LocalTensor<int32_t> t9 = tmp9.Get<int32_t>();
        LocalTensor<int32_t> t10 = tmp10.Get<int32_t>();
        LocalTensor<int32_t> t11 = tmp11.Get<int32_t>();
        LocalTensor<int32_t> t12 = tmp12.Get<int32_t>();

        for (int32_t i = 0; i < kPanel; ++i) {
            for (int32_t j = 0; j < kPanel; ++j) {
                transposeLut.SetValue(j * kPanel + i, static_cast<uint32_t>((i * kPanel + j) * sizeof(int32_t)));
                select0Lut.SetValue(i * kPanel + j, static_cast<uint32_t>((i * kFactor + j) * sizeof(int32_t)));
                select1Lut.SetValue(
                    i * kPanel + j, static_cast<uint32_t>((i * kFactor + kPanel + j) * sizeof(int32_t)));
            }
        }

        const int32_t rowStart = rowPanelIdx * kPanel;
        AscendC::GlobalTensor<int32_t> twiddleGm;
        twiddleGm.SetGlobalBuffer(activeTwiddlePhase12Base_ + static_cast<size_t>(rowStart) * kFactor, kHalfElemCount);
        AscendC::DataCopy(panelInFullBuf, twiddleGm, kHalfElemCount);
        AscendC::PipeBarrier<PIPE_ALL>();
        AscendC::Gather(panelTwiddleBuf, panelInFullBuf, select0Lut, 0U, static_cast<uint32_t>(kPanelElemCount));
        AscendC::PipeBarrier<PIPE_ALL>();
        ExtractInt10DigitVec(t4, panelTwiddleBuf, 0, t12, kPanelElemCount);
        ExtractInt10DigitVec(t5, panelTwiddleBuf, kFastDigitBits, t12, kPanelElemCount);
        ExtractInt10DigitVec(t6, panelTwiddleBuf, kFastDigitBits * 2, t12, kPanelElemCount);
        AscendC::Gather(panelTwiddleBuf, panelInFullBuf, select1Lut, 0U, static_cast<uint32_t>(kPanelElemCount));
        AscendC::PipeBarrier<PIPE_ALL>();
        ExtractInt10DigitVec(t7, panelTwiddleBuf, 0, t12, kPanelElemCount);
        ExtractInt10DigitVec(t8, panelTwiddleBuf, kFastDigitBits, t12, kPanelElemCount);
        ExtractInt10DigitVec(t9, panelTwiddleBuf, kFastDigitBits * 2, t12, kPanelElemCount);

        for (int32_t poly = polyStart; poly < polyEnd; ++poly) {
            const size_t polyOffset = static_cast<size_t>(poly) * kFactor * kFactor;

            AscendC::GlobalTensor<int32_t> srcGm;
            srcGm.SetGlobalBuffer(
                workPhase1Base_ + polyOffset + static_cast<size_t>(rowStart) * kFactor, kHalfElemCount);
            AscendC::DataCopy(panelInFullBuf, srcGm, kHalfElemCount);
            AscendC::PipeBarrier<PIPE_ALL>();

            AscendC::Gather(panelInBuf, panelInFullBuf, select0Lut, 0U, static_cast<uint32_t>(kPanelElemCount));
            AscendC::PipeBarrier<PIPE_ALL>();
            MulModVecVecInt10SchoolbookPresplitRhs(
                panelMulBuf, panelInBuf, activeQ_, activeFold28_, t4, t5, t6, t0, t1, t2, t3, t10, t11, t12, t11,
                activeUseInt10TwoWrap_, activeUseInt10OneWrap_, kPanelElemCount);
            AscendC::PipeBarrier<PIPE_ALL>();
            AscendC::Gather(panelOutBuf, panelMulBuf, transposeLut, 0U, static_cast<uint32_t>(kPanelElemCount));
            AscendC::PipeBarrier<PIPE_ALL>();
            for (int32_t j = 0; j < kPanel; ++j) {
                AscendC::GlobalTensor<int32_t> dstGm;
                dstGm.SetGlobalBuffer(
                    workPhase2Base_ + polyOffset + static_cast<size_t>(j) * kFactor + rowStart, kPanel);
                AscendC::DataCopy(dstGm, panelOutBuf[j * kPanel], kPanel);
            }

            AscendC::Gather(panelInBuf, panelInFullBuf, select1Lut, 0U, static_cast<uint32_t>(kPanelElemCount));
            AscendC::PipeBarrier<PIPE_ALL>();
            MulModVecVecInt10SchoolbookPresplitRhs(
                panelMulBuf, panelInBuf, activeQ_, activeFold28_, t7, t8, t9, t0, t1, t2, t3, t10, t11, t12, t11,
                activeUseInt10TwoWrap_, activeUseInt10OneWrap_, kPanelElemCount);
            AscendC::PipeBarrier<PIPE_ALL>();
            AscendC::Gather(panelOutBuf, panelMulBuf, transposeLut, 0U, static_cast<uint32_t>(kPanelElemCount));
            AscendC::PipeBarrier<PIPE_ALL>();
            for (int32_t j = 0; j < kPanel; ++j) {
                AscendC::GlobalTensor<int32_t> dstGm;
                dstGm.SetGlobalBuffer(
                    workPhase2Base_ + polyOffset + static_cast<size_t>(kPanel + j) * kFactor + rowStart, kPanel);
                AscendC::DataCopy(dstGm, panelOutBuf[j * kPanel], kPanel);
            }
            AscendC::PipeBarrier<PIPE_ALL>();
        }
    }

    __aicore__ inline void PreparePhase2Input()
    {
        if (isAic_) {
            return;
        }
        if (tiling_.n1 == 64 && tiling_.n2 == 64 && kTransposePanel == 32 && aivCoreCount_ >= 4) {
            if (activeUseInt10_) {
                PreparePhase2Input64x64RowPairInt10();
                return;
            }
        }

        const int32_t workerId = logicalBlockId_ * kMixAivPerAic + subBlockId_;
        const int32_t workerCount = aivCoreCount_;
        const int32_t rowPanelCount = CeilDivInt(tiling_.n1, kTransposePanel);
        const int32_t colPanelCount = CeilDivInt(tiling_.n2, kTransposePanel);
        const int32_t panelsPerPoly = rowPanelCount * colPanelCount;
        const int32_t totalPanels = tiling_.polyCount * panelsPerPoly;
        const int32_t panelsPerWorker = CeilDivInt(totalPanels, workerCount);
        const int32_t panelStart = workerId * panelsPerWorker;
        const int32_t panelEnd = MinInt(totalPanels, panelStart + panelsPerWorker);
        if (panelStart >= panelEnd) {
            return;
        }

        constexpr int32_t panelElemCount = kTransposePanel * kTransposePanel;
        AscendC::TPipe pipe;
        AscendC::TBuf<AscendC::TPosition::VECCALC> bufIn, bufTwiddle, bufMul, bufOut, bufTranspose;
        AscendC::TBuf<AscendC::TPosition::VECCALC> tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6;
        AscendC::TBuf<AscendC::TPosition::VECCALC> tmp7, tmp8, tmp9, tmp10, tmp11, tmp12;
        pipe.InitBuffer(bufIn, panelElemCount * sizeof(int32_t));
        pipe.InitBuffer(bufTwiddle, panelElemCount * sizeof(int32_t));
        pipe.InitBuffer(bufMul, panelElemCount * sizeof(int32_t));
        pipe.InitBuffer(bufOut, panelElemCount * sizeof(int32_t));
        pipe.InitBuffer(bufTranspose, panelElemCount * sizeof(uint32_t));
        pipe.InitBuffer(tmp0, panelElemCount * sizeof(int32_t));
        pipe.InitBuffer(tmp1, panelElemCount * sizeof(int32_t));
        pipe.InitBuffer(tmp2, panelElemCount * sizeof(int32_t));
        pipe.InitBuffer(tmp3, panelElemCount * sizeof(int32_t));
        pipe.InitBuffer(tmp4, panelElemCount * sizeof(int32_t));
        pipe.InitBuffer(tmp5, panelElemCount * sizeof(int32_t));
        pipe.InitBuffer(tmp6, panelElemCount * sizeof(int32_t));
        pipe.InitBuffer(tmp7, panelElemCount * sizeof(int32_t));
        pipe.InitBuffer(tmp8, panelElemCount * sizeof(int32_t));
        pipe.InitBuffer(tmp9, panelElemCount * sizeof(int32_t));
        pipe.InitBuffer(tmp10, panelElemCount * sizeof(int32_t));
        pipe.InitBuffer(tmp11, panelElemCount * sizeof(int32_t));
        pipe.InitBuffer(tmp12, panelElemCount * sizeof(int32_t));

        LocalTensor<int32_t> panelInBuf = bufIn.Get<int32_t>();
        LocalTensor<int32_t> panelTwiddleBuf = bufTwiddle.Get<int32_t>();
        LocalTensor<int32_t> panelMulBuf = bufMul.Get<int32_t>();
        LocalTensor<int32_t> panelOutBuf = bufOut.Get<int32_t>();
        LocalTensor<uint32_t> transposeLut = bufTranspose.Get<uint32_t>();
        LocalTensor<int32_t> t0 = tmp0.Get<int32_t>();
        LocalTensor<int32_t> t1 = tmp1.Get<int32_t>();
        LocalTensor<int32_t> t2 = tmp2.Get<int32_t>();
        LocalTensor<int32_t> t3 = tmp3.Get<int32_t>();
        LocalTensor<int32_t> t4 = tmp4.Get<int32_t>();
        LocalTensor<int32_t> t5 = tmp5.Get<int32_t>();
        LocalTensor<int32_t> t6 = tmp6.Get<int32_t>();
        LocalTensor<int32_t> t7 = tmp7.Get<int32_t>();
        LocalTensor<int32_t> t8 = tmp8.Get<int32_t>();
        LocalTensor<int32_t> t9 = tmp9.Get<int32_t>();
        LocalTensor<int32_t> t10 = tmp10.Get<int32_t>();
        LocalTensor<int32_t> t11 = tmp11.Get<int32_t>();
        LocalTensor<int32_t> t12 = tmp12.Get<int32_t>();

        for (int32_t i = 0; i < kTransposePanel; ++i) {
            for (int32_t j = 0; j < kTransposePanel; ++j) {
                transposeLut.SetValue(
                    j * kTransposePanel + i, static_cast<uint32_t>((i * kTransposePanel + j) * sizeof(int32_t)));
            }
        }

        for (int32_t localPanelIdx = 0; localPanelIdx < panelsPerPoly; ++localPanelIdx) {
            int32_t firstPanelIdx = localPanelIdx;
            if (firstPanelIdx < panelStart) {
                firstPanelIdx += CeilDivInt(panelStart - firstPanelIdx, panelsPerPoly) * panelsPerPoly;
            }
            if (firstPanelIdx >= panelEnd) {
                continue;
            }

            const int32_t rowPanelIdx = localPanelIdx / colPanelCount;
            const int32_t colPanelIdx = localPanelIdx - rowPanelIdx * colPanelCount;
            const int32_t rowStart = rowPanelIdx * kTransposePanel;
            const int32_t colStart = colPanelIdx * kTransposePanel;
            const int32_t panelRows = MinInt(kTransposePanel, tiling_.n1 - rowStart);
            const int32_t panelCols = MinInt(kTransposePanel, tiling_.n2 - colStart);

            if (panelRows != kTransposePanel || panelCols != kTransposePanel) {
                AscendC::Duplicate(panelTwiddleBuf, static_cast<int32_t>(0), panelElemCount);
            }

            for (int32_t i = 0; i < panelRows; ++i) {
                const size_t rowOffset = static_cast<size_t>(rowStart + i) * tiling_.n2 + colStart;
                AscendC::GlobalTensor<int32_t> twiddleGm;
                twiddleGm.SetGlobalBuffer(activeTwiddlePhase12Base_ + rowOffset, panelCols);
                if ((panelCols % 8) == 0) {
                    AscendC::DataCopy(panelTwiddleBuf[i * kTransposePanel], twiddleGm, panelCols);
                } else {
                    for (int32_t j = 0; j < panelCols; ++j) {
                        panelTwiddleBuf.SetValue(i * kTransposePanel + j, twiddleGm.GetValue(j));
                    }
                }
            }

            AscendC::PipeBarrier<PIPE_ALL>();
            if (activeUseInt10_) {
                ExtractInt10DigitVec(t4, panelTwiddleBuf, 0, t12, panelElemCount);
                ExtractInt10DigitVec(t5, panelTwiddleBuf, kFastDigitBits, t12, panelElemCount);
                ExtractInt10DigitVec(t6, panelTwiddleBuf, kFastDigitBits * 2, t12, panelElemCount);
            } else if (activeBarrettK_ > kDefaultBarrettK) {
                ExtractInt7DigitVec(t4, panelTwiddleBuf, 0, t12, panelElemCount);
                ExtractInt7DigitVec(t5, panelTwiddleBuf, kDigitBaseBits, t12, panelElemCount);
                ExtractInt7DigitVec(t6, panelTwiddleBuf, kDigitBaseBits * 2, t12, panelElemCount);
                ExtractInt7DigitVec(t7, panelTwiddleBuf, kDigitBaseBits * 3, t12, panelElemCount);
            }

            for (int32_t panelIdx = firstPanelIdx; panelIdx < panelEnd; panelIdx += panelsPerPoly) {
                const int32_t poly = panelIdx / panelsPerPoly;
                const size_t polyOffset = static_cast<size_t>(poly) * tiling_.n;

                if (panelRows != kTransposePanel || panelCols != kTransposePanel) {
                    AscendC::Duplicate(panelInBuf, static_cast<int32_t>(0), panelElemCount);
                }

                for (int32_t i = 0; i < panelRows; ++i) {
                    const size_t rowOffset = static_cast<size_t>(rowStart + i) * tiling_.n2 + colStart;
                    AscendC::GlobalTensor<int32_t> srcGm;
                    srcGm.SetGlobalBuffer(workPhase1Base_ + polyOffset + rowOffset, panelCols);
                    if ((panelCols % 8) == 0) {
                        AscendC::DataCopy(panelInBuf[i * kTransposePanel], srcGm, panelCols);
                    } else {
                        for (int32_t j = 0; j < panelCols; ++j) {
                            panelInBuf.SetValue(i * kTransposePanel + j, srcGm.GetValue(j));
                        }
                    }
                }

                AscendC::PipeBarrier<PIPE_ALL>();
                if (activeBarrettK_ <= kDefaultBarrettK) {
                    MulModVecVecInt7Horner(
                        panelMulBuf, panelInBuf, panelTwiddleBuf, activeQ_, activeBarrettK_,
                        static_cast<int32_t>(activeBarrettMu_), t0, t1, t2, t3, t4, panelElemCount);
                } else if (activeUseInt10_) {
                    MulModVecVecInt10SchoolbookPresplitRhs(
                        panelMulBuf, panelInBuf, activeQ_, activeFold28_, t4, t5, t6, t0, t1, t2, t8, t9, t10, t11, t12,
                        activeUseInt10TwoWrap_, activeUseInt10OneWrap_, panelElemCount);
                } else {
                    MulModVecVecInt7SchoolbookPresplitRhs(
                        panelMulBuf, panelInBuf, activeQ_, activeFold28_, t4, t5, t6, t7, t0, t1, t2, t3, t8, t9, t10,
                        t11, t12, panelElemCount);
                }
                AscendC::PipeBarrier<PIPE_ALL>();
                AscendC::Gather(panelOutBuf, panelMulBuf, transposeLut, 0U, static_cast<uint32_t>(panelElemCount));
                AscendC::PipeBarrier<PIPE_ALL>();

                for (int32_t j = 0; j < panelCols; ++j) {
                    AscendC::GlobalTensor<int32_t> dstGm;
                    dstGm.SetGlobalBuffer(
                        workPhase2Base_ + polyOffset + static_cast<size_t>(colStart + j) * tiling_.n1 + rowStart,
                        panelRows);
                    if ((panelRows % 8) == 0) {
                        AscendC::DataCopy(dstGm, panelOutBuf[j * kTransposePanel], panelRows);
                    } else {
                        for (int32_t i = 0; i < panelRows; ++i) {
                            dstGm.SetValue(i, panelOutBuf.GetValue(j * kTransposePanel + i));
                        }
                    }
                }
                AscendC::PipeBarrier<PIPE_ALL>();
            }
        }
    }

    __aicore__ inline void SyncAllCores()
    {
        AscendC::SyncAll<false>();
    }

private:
    NttTilingData tiling_{};
    FourStepWorkspaceLayout layout_{};
    bool isAic_ = false;
    bool active_ = false;
    int32_t subBlockId_ = 0;
    int32_t logicalBlockId_ = 0;
    int32_t logicalBlockCount_ = 0;
    int32_t aivCoreCount_ = 0;
    int32_t channelBlock_ = 1;
    int32_t channelLaneId_ = 0;

    __gm__ int32_t* xBase_ = nullptr;
    __gm__ int32_t* zBase_ = nullptr;
    __gm__ int32_t* workPhase1Base_ = nullptr;
    __gm__ int32_t* workPhase2Base_ = nullptr;
    GM_ADDR scratchBase_ = nullptr;
    GM_ADDR matrixPhase1_ = nullptr;
    GM_ADDR matrixPhase2_ = nullptr;
    __gm__ int32_t* twiddlePhase12Base_ = nullptr;
    __gm__ int32_t* activeTwiddlePhase12Base_ = nullptr;
    int32_t rnsChannels_ = 1;
    int32_t activeQ_ = 0;
    int32_t activeBarrettK_ = kDefaultBarrettK;
    int32_t activeFold28_ = 0;
    bool activeUseInt10_ = false;
    bool activeUseInt10TwoWrap_ = false;
    bool activeUseInt10OneWrap_ = false;
    uint64_t activeBarrettMu_ = 0;

    AscendC::GlobalTensor<int32_t> workPhase1Gm_;
    AscendC::GlobalTensor<int32_t> workPhase2Gm_;
};
