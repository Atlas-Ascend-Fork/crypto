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

__aicore__ inline void CubeNtt::Init(
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

__aicore__ inline void CubeNtt::Process()
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

__aicore__ inline void CubeNtt::ProcessActiveChannel(int32_t channel)
{
    __gm__ int32_t* xChannelBase = xBase_ + RnsChannelElemOffset(channel);
    __gm__ int32_t* zChannelBase = zBase_ + RnsChannelElemOffset(channel);
    GM_ADDR matrixPhase1 = MatrixPhase1ForChannel(channel);
    GM_ADDR matrixPhase2 = MatrixPhase2ForChannel(channel);

    if (!tiling_.useFourStep) {
        if (UseParallelDirectBat()) {
            ProcessParallelDirectBat(xChannelBase, zChannelBase, matrixPhase1);
            return;
        }
        SubNTT(
            xChannelBase, tiling_.n, zChannelBase, tiling_.n, tiling_.phase1KernelN, tiling_.n, tiling_.polyCount,
            matrixPhase1);
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

__aicore__ inline void CubeNtt::ProcessInactiveChannel()
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

__aicore__ inline size_t CubeNtt::RnsChannelElemOffset(int32_t channel) const
{
    return static_cast<size_t>(channel) * static_cast<size_t>(tiling_.polyCount) * static_cast<size_t>(tiling_.n);
}

__aicore__ inline int32_t CubeNtt::ChannelQ(int32_t channel) const
{
    if (tiling_.rnsChannels > 0 && tiling_.qList[channel] > 0) {
        return tiling_.qList[channel];
    }
    return tiling_.q;
}

__aicore__ inline GM_ADDR CubeNtt::MatrixPhase1ForChannel(int32_t channel) const
{
    return matrixPhase1_ + static_cast<size_t>(channel) * BatMatrixBytesInt(tiling_.phase1KernelN);
}

__aicore__ inline size_t CubeNtt::TwiddleCountPerChannel() const
{
    return tiling_.useFourStep ? static_cast<size_t>(tiling_.n1) * static_cast<size_t>(tiling_.n2) : 1;
}

__aicore__ inline void CubeNtt::SetActiveChannel(int32_t channel)
{
    activeQ_ = ChannelQ(channel);
    activeBarrettK_ = ComputeBarrettK(activeQ_);
    activeBarrettMu_ = ComputeBarrettMu(activeQ_, activeBarrettK_);
    activeFold28_ = (activeBarrettK_ == kResidueBits && activeQ_ > (kResidueBase / 2)) ? (kResidueBase - activeQ_) : 0;
    activeUseInt10_ = CanUseInt10Fold(activeFold28_);
    activeUseInt10TwoWrap_ = CanSkipInt10FirstWrap(activeFold28_);
    activeUseInt10OneWrap_ = CanSkipInt10SecondWrap(activeFold28_);
    activeTwiddlePhase12Base_ = twiddlePhase12Base_ + static_cast<size_t>(channel) * TwiddleCountPerChannel();
}

__aicore__ inline bool CubeNtt::UseParallelDirectBat() const
{
    return tiling_.n == 256 && tiling_.phase1KernelN == 256 && tiling_.rnsChannels == 1 && tiling_.channelBlock == 1 &&
           tiling_.blockDim == kBatTileCount && tiling_.subBench == 16 && tiling_.polyCount > 0 &&
           (activeQ_ == 3329 || activeQ_ == 8380417);
}

__aicore__ inline int32_t CubeNtt::ParallelBatPaddedPolyCount() const
{
    return AlignUpInt(tiling_.polyCount, tiling_.subBench);
}

__aicore__ inline size_t CubeNtt::ParallelBatPartialMatrixBytes() const
{
    return static_cast<size_t>(ParallelBatPaddedPolyCount()) * static_cast<size_t>(tiling_.n) * sizeof(int32_t);
}

__aicore__ inline GM_ADDR CubeNtt::ParallelBatPartialMatrix(int32_t matrixId) const
{
    return scratchBase_ + static_cast<size_t>(kDigitCount) * ParallelBatSplitPlaneBytes() +
           static_cast<size_t>(matrixId) * ParallelBatPartialMatrixBytes();
}

__aicore__ inline void CubeNtt::ProcessParallelDirectBat(
    __gm__ int32_t* srcBase, __gm__ int32_t* dstBase, GM_ADDR matrixBase)
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

__aicore__ inline int32_t CubeNtt::ParallelPqcTileCount() const
{
    return CeilDivInt(ParallelBatPaddedPolyCount(), kParallelAivPolyBatch);
}

__aicore__ inline int32_t CubeNtt::ParallelPqcRoundCount() const
{
    return CeilDivInt(ParallelPqcTileCount(), logicalBlockCount_);
}

__aicore__ inline uint16_t CubeNtt::PqcSplitState(int32_t slot) const { return static_cast<uint16_t>(2 * slot); }

__aicore__ inline uint16_t CubeNtt::PqcMergeState(int32_t slot) const { return static_cast<uint16_t>(2 * slot + 1); }

__aicore__ inline void CubeNtt::RunPqcCubePipeline(GM_ADDR matrixBase, int32_t roundCount, bool kem)
{
    constexpr int32_t pipelineDepth = 2;
    for (int32_t round = 0; round < roundCount; ++round) {
        const int32_t tileId = round * logicalBlockCount_ + logicalBlockId_;
        if (tileId >= ParallelPqcTileCount()) {
            continue;
        }
        const int32_t slot = round % pipelineDepth;
        __WAIT(PqcSplitState(slot));
        PqcTileMmad(matrixBase, tileId, kem);
        __SET(PqcMergeState(slot), true);
    }
}

__aicore__ inline void CubeNtt::ProcessPqcTilePipeline(
    __gm__ int32_t* srcBase, __gm__ int32_t* dstBase, GM_ADDR matrixBase, bool kem)
{
    constexpr int32_t pipelineDepth = 2;
    const int32_t roundCount = ParallelPqcRoundCount();
    if (isAic_) {
        RunPqcCubePipeline(matrixBase, roundCount, kem);
        return;
    }

    const int32_t prefill = MinInt(pipelineDepth, roundCount);
    for (int32_t round = 0; round < prefill; ++round) {
        const int32_t tileId = round * logicalBlockCount_ + logicalBlockId_;
        if (tileId >= ParallelPqcTileCount()) {
            continue;
        }
        const int32_t slot = round % pipelineDepth;
        PqcTileSplit(srcBase, tileId, kem);
        __SET(PqcSplitState(slot), false);
    }

    for (int32_t round = 0; round < roundCount; ++round) {
        const int32_t tileId = round * logicalBlockCount_ + logicalBlockId_;
        if (tileId >= ParallelPqcTileCount()) {
            continue;
        }
        const int32_t slot = round % pipelineDepth;
        __WAIT(PqcMergeState(slot));
        PqcTileMerge(dstBase, tileId, kem);

        const int32_t nextRound = round + pipelineDepth;
        const int32_t nextTile = nextRound * logicalBlockCount_ + logicalBlockId_;
        if (nextRound < roundCount && nextTile < ParallelPqcTileCount()) {
            const int32_t nextSlot = nextRound % pipelineDepth;
            PqcTileSplit(srcBase, nextTile, kem);
            __SET(PqcSplitState(nextSlot), false);
        }
    }
}

__aicore__ inline void CubeNtt::PqcTileMmad(GM_ADDR matrix, int32_t tile, bool kem)
{
    if (kem) {
        ParallelKemParityMmad(matrix, tile);
    } else {
        ParallelDsaTileMmad(matrix, tile);
    }
}

__aicore__ inline void CubeNtt::PqcTileSplit(__gm__ int32_t* src, int32_t tile, bool kem)
{
    if (kem) {
        ParallelKemParitySplit(src, tile);
    } else {
        ParallelBatSplit(src, tile);
    }
}

__aicore__ inline void CubeNtt::PqcTileMerge(__gm__ int32_t* dst, int32_t tile, bool kem)
{
    if (kem) {
        ParallelKemParityMerge(dst, tile);
    } else {
        ParallelBatMerge(dst, tile);
    }
}

__aicore__ inline void CubeNtt::ProcessKemTilePipeline(
    __gm__ int32_t* srcBase, __gm__ int32_t* dstBase, GM_ADDR matrixBase)
{
    ProcessPqcTilePipeline(srcBase, dstBase, matrixBase, true);
}

__aicore__ inline void CubeNtt::ProcessDsaTilePipeline(
    __gm__ int32_t* srcBase, __gm__ int32_t* dstBase, GM_ADDR matrixBase)
{
    ProcessPqcTilePipeline(srcBase, dstBase, matrixBase, false);
}

__aicore__ inline bool CubeNtt::CanRunSubNttPipeline(int32_t roundCount)
{
    return roundCount > 1 && static_cast<int32_t>(kPipelinedCrossCoreStates) <= kFirstSyncAllReservedFlag;
}

__aicore__ inline void CubeNtt::SyncAllCores() { AscendC::SyncAll<false>(); }
