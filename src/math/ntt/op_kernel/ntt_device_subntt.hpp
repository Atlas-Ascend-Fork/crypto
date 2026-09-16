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

__aicore__ inline void CubeNtt::SubNTT(
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

__aicore__ inline uint16_t CubeNtt::SubNttStateId(int32_t slot, uint16_t statesPerBlock)
{
    (void)statesPerBlock;
    return static_cast<uint16_t>(slot);
}

__aicore__ inline GM_ADDR CubeNtt::ScratchSlotBase(size_t scratchPerBlock, int32_t slot)
{
    const size_t logicalSlot =
        static_cast<size_t>(logicalBlockId_) * static_cast<size_t>(kSubNttPipelineSlots) + static_cast<size_t>(slot);
    return scratchBase_ + logicalSlot * scratchPerBlock;
}

__aicore__ inline void CubeNtt::RoundBlockBench(
    int32_t round, int32_t roundSpan, int32_t totalBench, int32_t& blockBenchOffset, int32_t& actualBench)
{
    blockBenchOffset = round * roundSpan + logicalBlockId_ * tiling_.subBench;
    const int32_t remaining = totalBench - blockBenchOffset;
    actualBench = remaining > 0 ? MinInt(tiling_.subBench, remaining) : 0;
}

__aicore__ inline bool CubeNtt::RoundHasWork(int32_t round, int32_t roundSpan, int32_t totalBench)
{
    return round * roundSpan + logicalBlockId_ * tiling_.subBench < totalBench;
}

__aicore__ inline void CubeNtt::RunSubNTTSerialTask(
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
        srcBase, srcStride, kernelN, logicalN, totalBench, paddedBenchWidth, scratchPerBlock, roundSpan, round, slot);
    __SET(stateId, isAic_);
    __WAIT(stateId);
    RunSubNTTMergeTask(
        dstBase, dstStride, kernelN, logicalN, totalBench, paddedBenchWidth, scratchPerBlock, roundSpan, round, slot);
}

__aicore__ inline void CubeNtt::RunSubNTTPipeline(
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

__aicore__ inline NttScratch CubeNtt::MakeScratch(
    int32_t kernelN, int32_t paddedBenchWidth, size_t scratchPerBlock, int32_t slot)
{
    const size_t elements = static_cast<size_t>(kernelN) * paddedBenchWidth;
    return {ScratchSlotBase(scratchPerBlock, slot), elements * sizeof(int8_t), elements * sizeof(int32_t)};
}

__aicore__ inline void CubeNtt::RunSubNTTMmadTask(
    GM_ADDR matrixBase, int32_t kernelN, int32_t paddedBenchWidth, size_t scratchPerBlock, int32_t slot)
{
    const size_t matrixTileBytes = static_cast<size_t>(kernelN) * kernelN;
    const auto scratch = MakeScratch(kernelN, paddedBenchWidth, scratchPerBlock, slot);
    AicMmad mmol(paddedBenchWidth, kernelN, kernelN);
    mmol.Init();
    LocalTensor<int32_t> accum[4] = {mmol.AllocAccum(), mmol.AllocAccum(), mmol.AllocAccum(), mmol.AllocAccum()};
    for (int32_t input = 0; input < kDigitCount; ++input) {
        mmol.LoadA(scratch.Split(input));
        for (int32_t output = 0; output < kDigitCount; ++output) {
            mmol.LoadB(matrixBase + (static_cast<size_t>(output * kDigitCount + input) * matrixTileBytes));
            mmol.ComputeAccum(accum[output], input == 0);
            mmol.FreeB();
        }
        mmol.FreeA();
    }
    for (int32_t output = 0; output < kDigitCount; ++output) {
        mmol.EnqueAccum(accum[output]);
    }
    for (int32_t output = 0; output < kDigitCount; ++output) {
        mmol.CopyOutQueued(scratch.Accum(output));
    }
}

__aicore__ inline void CubeNtt::RunSubNTTSplitTask(
    __gm__ int32_t* srcBase, int32_t srcStride, int32_t kernelN, int32_t logicalN, int32_t totalBench,
    int32_t paddedBenchWidth, size_t scratchPerBlock, int32_t roundSpan, int32_t round, int32_t slot)
{
    int32_t blockBenchOffset = 0;
    int32_t actualBench = 0;
    RoundBlockBench(round, roundSpan, totalBench, blockBenchOffset, actualBench);
    const auto scratch = MakeScratch(kernelN, paddedBenchWidth, scratchPerBlock, slot);

    DirectSplitBlockHalf(
        scratch.Split(0), scratch.Split(1), scratch.Split(2), scratch.Split(3), srcBase, srcStride, blockBenchOffset,
        actualBench, logicalN, kernelN, paddedBenchWidth);
    AscendC::PipeBarrier<PIPE_ALL>();
}

__aicore__ inline void CubeNtt::RunSubNTTMergeTask(
    __gm__ int32_t* dstBase, int32_t dstStride, int32_t kernelN, int32_t logicalN, int32_t totalBench,
    int32_t paddedBenchWidth, size_t scratchPerBlock, int32_t roundSpan, int32_t round, int32_t slot)
{
    int32_t blockBenchOffset = 0;
    int32_t actualBench = 0;
    RoundBlockBench(round, roundSpan, totalBench, blockBenchOffset, actualBench);
    const auto scratch = MakeScratch(kernelN, paddedBenchWidth, scratchPerBlock, slot);

    BatAivCombine4 merge(
        subBlockId_, kernelN, paddedBenchWidth, activeQ_, activeBarrettK_, activeBarrettMu_, activeFold28_);
    merge.Init(
        dstBase, dstStride, blockBenchOffset, actualBench, logicalN, scratch.Accum(0), scratch.Accum(1),
        scratch.Accum(2), scratch.Accum(3));
    merge.CopyIn();
    merge.Compute();
    merge.CopyOut();
}

__aicore__ inline void CubeNtt::LoadDirectSplitInput(
    NttSplitBuffers& buffers, __gm__ int32_t* srcBase, const DirectSplitPlan& plan)
{
    LocalTensor<int32_t> input = buffers.input.AllocTensor<int32_t>();
    if (plan.validRows != plan.halfRows || !plan.denseBenchLayout) {
        AscendC::Duplicate(input, static_cast<int32_t>(0), plan.tileLength);
    }
    if (plan.validRows > 0) {
        if (plan.denseBenchLayout) {
            AscendC::GlobalTensor<int32_t> src;
            src.SetGlobalBuffer(
                srcBase + static_cast<size_t>(plan.blockBenchOffset + plan.rowStart) * plan.srcStride,
                static_cast<size_t>(plan.validRows) * plan.kernelN);
            AscendC::DataCopy(input, src, plan.validRows * plan.kernelN);
        } else {
            for (int32_t row = 0; row < plan.validRows; ++row) {
                AscendC::GlobalTensor<int32_t> src;
                src.SetGlobalBuffer(
                    srcBase + static_cast<size_t>(plan.blockBenchOffset + plan.rowStart + row) * plan.srcStride,
                    plan.logicalN);
                AscendC::DataCopy(input[row * plan.kernelN], src, plan.logicalN);
            }
        }
    }
    buffers.input.EnQue(input);
}

__aicore__ inline void CubeNtt::ComputeDirectSplit(NttSplitBuffers& buffers, int32_t length)
{
    LocalTensor<int32_t> input = buffers.input.DeQue<int32_t>();
    LocalTensor<int8_t> output[4] = {
        buffers.output[0].AllocTensor<int8_t>(), buffers.output[1].AllocTensor<int8_t>(),
        buffers.output[2].AllocTensor<int8_t>(), buffers.output[3].AllocTensor<int8_t>()};
    LocalTensor<int32_t> tmp0 = buffers.tmp0.Get<int32_t>();
    LocalTensor<half> tmp1 = buffers.tmp1.Get<half>();
    Tensor_int8x4 digits{output[0], output[1], output[2], output[3]};
    split_vec_int7x4(digits, input, tmp0, tmp1, length);
    for (int32_t i = 0; i < 4; ++i) {
        buffers.output[i].EnQue(output[i]);
    }
    buffers.input.FreeTensor(input);
}

__aicore__ inline void CubeNtt::StoreDirectSplit(
    NttSplitBuffers& buffers, GM_ADDR* destinations, const DirectSplitPlan& plan)
{
    LocalTensor<int8_t> output[4] = {
        buffers.output[0].DeQue<int8_t>(), buffers.output[1].DeQue<int8_t>(), buffers.output[2].DeQue<int8_t>(),
        buffers.output[3].DeQue<int8_t>()};
    for (int32_t i = 0; i < 4; ++i) {
        AscendC::GlobalTensor<int8_t> dst;
        dst.SetGlobalBuffer(
            reinterpret_cast<__gm__ int8_t*>(destinations[i]),
            static_cast<size_t>(plan.kernelN) * plan.paddedBenchWidth);
        AscendC::DataCopy(dst[subBlockId_ * plan.tileLength], output[i], plan.tileLength);
    }
    AscendC::PipeBarrier<PIPE_ALL>();
    for (int32_t i = 0; i < 4; ++i) {
        buffers.output[i].FreeTensor(output[i]);
    }
}

__aicore__ inline void CubeNtt::DirectSplitBlockHalf(
    GM_ADDR dst0, GM_ADDR dst1, GM_ADDR dst2, GM_ADDR dst3, __gm__ int32_t* srcBase, int32_t srcStride,
    int32_t blockBenchOffset, int32_t actualBench, int32_t logicalN, int32_t kernelN, int32_t paddedBenchWidth)
{
    const int32_t halfRows = paddedBenchWidth / kMixAivPerAic;
    const int32_t rowStart = subBlockId_ * halfRows;
    const int32_t validRows = actualBench > rowStart ? MinInt(halfRows, actualBench - rowStart) : 0;
    const DirectSplitPlan plan{
        srcStride, blockBenchOffset, logicalN,           kernelN,   paddedBenchWidth,
        halfRows,  rowStart,         kernelN * halfRows, validRows, srcStride == logicalN && logicalN == kernelN};
    NttSplitBuffers buffers;
    buffers.Init(plan.tileLength, false);
    LoadDirectSplitInput(buffers, srcBase, plan);
    ComputeDirectSplit(buffers, plan.tileLength);
    GM_ADDR destinations[4] = {dst0, dst1, dst2, dst3};
    StoreDirectSplit(buffers, destinations, plan);
}
