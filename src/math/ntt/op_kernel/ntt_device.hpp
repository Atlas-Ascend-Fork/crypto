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
#include "ntt_bat_combine.hpp"

class CubeNtt {
public:
    __aicore__ inline void Init(
        GM_ADDR x, GM_ADDR z, GM_ADDR workspace, GM_ADDR matrixPhase1, GM_ADDR matrixPhase2, GM_ADDR twiddlePhase12,
        const NttTilingData& tiling);

    __aicore__ inline void Process();

private:
    __aicore__ inline void ProcessActiveChannel(int32_t channel);

    __aicore__ inline void ProcessInactiveChannel();

    __aicore__ inline size_t RnsChannelElemOffset(int32_t channel) const;

    __aicore__ inline int32_t ChannelQ(int32_t channel) const;

    __aicore__ inline GM_ADDR MatrixPhase1ForChannel(int32_t channel) const;

    __aicore__ inline GM_ADDR MatrixPhase2ForChannel(int32_t channel) const;

    __aicore__ inline size_t TwiddleCountPerChannel() const;

    __aicore__ inline void SetActiveChannel(int32_t channel);

    __aicore__ inline bool UseParallelDirectBat() const;

    __aicore__ inline int32_t ParallelBatPaddedPolyCount() const;

    __aicore__ inline size_t ParallelBatSplitPlaneBytes() const;

    __aicore__ inline size_t ParallelBatPartialMatrixBytes() const;

    __aicore__ inline GM_ADDR ParallelBatSplitPlane(int32_t digit) const;

    __aicore__ inline GM_ADDR ParallelBatPartialMatrix(int32_t matrixId) const;

    __aicore__ inline void ProcessParallelDirectBat(
        __gm__ int32_t* srcBase, __gm__ int32_t* dstBase, GM_ADDR matrixBase);

    __aicore__ inline int32_t ParallelPqcTileCount() const;

    __aicore__ inline int32_t ParallelPqcRoundCount() const;

    __aicore__ inline uint16_t PqcSplitState(int32_t slot) const;

    __aicore__ inline uint16_t PqcMergeState(int32_t slot) const;

    __aicore__ inline void RunPqcCubePipeline(GM_ADDR matrixBase, int32_t roundCount, bool kem);

    __aicore__ inline void ProcessPqcTilePipeline(
        __gm__ int32_t* srcBase, __gm__ int32_t* dstBase, GM_ADDR matrixBase, bool kem);

    __aicore__ inline void PqcTileMmad(GM_ADDR matrix, int32_t tile, bool kem);

    __aicore__ inline void PqcTileSplit(__gm__ int32_t* src, int32_t tile, bool kem);

    __aicore__ inline void PqcTileMerge(__gm__ int32_t* dst, int32_t tile, bool kem);

    __aicore__ inline void ProcessKemTilePipeline(__gm__ int32_t* srcBase, __gm__ int32_t* dstBase, GM_ADDR matrixBase);

    __aicore__ inline void ProcessDsaTilePipeline(__gm__ int32_t* srcBase, __gm__ int32_t* dstBase, GM_ADDR matrixBase);

    __aicore__ inline void StoreKemParityDigits(
        LocalTensor<int8_t>& digit0, LocalTensor<int8_t>& digit1, int32_t polyStart);

    __aicore__ inline void ParallelKemParitySplit(__gm__ int32_t* srcBase, int32_t tileId);

    __aicore__ inline void ParallelKemParityMmad(GM_ADDR matrixBase, int32_t tileId);

    __aicore__ inline void ParallelDsaTileMmad(GM_ADDR matrixBase, int32_t tileId);

    __aicore__ inline void ParallelKemParityMerge(__gm__ int32_t* dstBase, int32_t tileId);

    __aicore__ inline PqcPanel MakePqcPanel(int32_t forcedTile) const;

    __aicore__ inline void LoadPqcInput(NttSplitBuffers& buffers, __gm__ int32_t* srcBase, const PqcPanel& panel);

    __aicore__ inline void ComputePqcSplit(NttSplitBuffers& buffers, int32_t tileLength);

    __aicore__ inline void StorePqcSplit(NttSplitBuffers& buffers, const PqcPanel& panel);

    __aicore__ inline void ParallelBatSplit(__gm__ int32_t* srcBase, int32_t forcedTile = -1);

    __aicore__ inline void MmadPqcTile(AicMmad& mm, int32_t matrixId, int32_t inDigit, int32_t tile);

    __aicore__ inline void ParallelKemMmadTiles(AicMmad& mm, GM_ADDR matrixBase, int32_t tileCount);

    __aicore__ inline void ParallelDsaMmadTiles(AicMmad& mm, GM_ADDR matrixBase, int32_t tileCount);

    __aicore__ inline void ParallelBatMmad(GM_ADDR matrixBase);

    __aicore__ inline void ParallelBatLoadDigitSum(
        LocalTensor<int32_t>& dst, LocalTensor<int32_t>& tmp, int32_t outDigit, int32_t polyStart, int32_t coeffStart,
        int32_t panelWidth, int32_t n);

    __aicore__ inline void ParallelBatReduce(
        LocalTensor<int32_t>& value, LocalTensor<int32_t>& tmp0, LocalTensor<int32_t>& tmp1, int32_t count,
        bool fastFold);

    __aicore__ inline void ParallelBatReduceSignedDsa(
        LocalTensor<int32_t>& value, LocalTensor<int32_t>& tmp0, LocalTensor<int32_t>& tmp1, int32_t count);

    __aicore__ inline void ParallelBatMulBase256Dsa(
        LocalTensor<int32_t>& value, LocalTensor<int32_t>& tmp0, LocalTensor<int32_t>& tmp1, int32_t count);

    __aicore__ inline void ParallelBatMergeStepDsa(
        LocalTensor<int32_t>& value, LocalTensor<int32_t>& addend, LocalTensor<int32_t>& tmp0,
        LocalTensor<int32_t>& tmp1, int32_t count);

    __aicore__ inline void ParallelBatMergeStep(
        LocalTensor<int32_t>& value, LocalTensor<int32_t>& addend, LocalTensor<int32_t>& tmp0,
        LocalTensor<int32_t>& tmp1, int32_t count, bool fastFold);

    __aicore__ inline void ComputePqcMerge(NttVectorBuffers<7>& buffers, const PqcPanel& panel);

    __aicore__ inline void ParallelBatMerge(__gm__ int32_t* dstBase, int32_t forcedTile = -1);

    __aicore__ inline void SubNTT(
        __gm__ int32_t* srcBase, int32_t srcStride, __gm__ int32_t* dstBase, int32_t dstStride, int32_t kernelN,
        int32_t logicalN, int32_t totalBench, GM_ADDR matrixBase);

    __aicore__ inline bool CanRunSubNttPipeline(int32_t roundCount);

    __aicore__ inline uint16_t SubNttStateId(int32_t slot, uint16_t statesPerBlock);

    __aicore__ inline GM_ADDR ScratchSlotBase(size_t scratchPerBlock, int32_t slot);

    __aicore__ inline void RoundBlockBench(
        int32_t round, int32_t roundSpan, int32_t totalBench, int32_t& blockBenchOffset, int32_t& actualBench);

    __aicore__ inline bool RoundHasWork(int32_t round, int32_t roundSpan, int32_t totalBench);

    __aicore__ inline void RunSubNTTSerialTask(
        __gm__ int32_t* srcBase, int32_t srcStride, __gm__ int32_t* dstBase, int32_t dstStride, GM_ADDR matrixBase,
        int32_t kernelN, int32_t logicalN, int32_t totalBench, int32_t paddedBenchWidth, size_t scratchPerBlock,
        int32_t roundSpan, int32_t round);

    __aicore__ inline void RunSubNTTPipeline(
        __gm__ int32_t* srcBase, int32_t srcStride, __gm__ int32_t* dstBase, int32_t dstStride, GM_ADDR matrixBase,
        int32_t kernelN, int32_t logicalN, int32_t totalBench, int32_t paddedBenchWidth, size_t scratchPerBlock,
        int32_t roundSpan, int32_t roundCount);

    __aicore__ inline NttScratch MakeScratch(
        int32_t kernelN, int32_t paddedBenchWidth, size_t scratchPerBlock, int32_t slot);

    __aicore__ inline void RunSubNTTMmadTask(
        GM_ADDR matrixBase, int32_t kernelN, int32_t paddedBenchWidth, size_t scratchPerBlock, int32_t slot);

    __aicore__ inline void RunSubNTTSplitTask(
        __gm__ int32_t* srcBase, int32_t srcStride, int32_t kernelN, int32_t logicalN, int32_t totalBench,
        int32_t paddedBenchWidth, size_t scratchPerBlock, int32_t roundSpan, int32_t round, int32_t slot);

    __aicore__ inline void RunSubNTTMergeTask(
        __gm__ int32_t* dstBase, int32_t dstStride, int32_t kernelN, int32_t logicalN, int32_t totalBench,
        int32_t paddedBenchWidth, size_t scratchPerBlock, int32_t roundSpan, int32_t round, int32_t slot);

    __aicore__ inline void LoadDirectSplitInput(
        NttSplitBuffers& buffers, __gm__ int32_t* srcBase, const DirectSplitPlan& plan);

    __aicore__ inline void ComputeDirectSplit(NttSplitBuffers& buffers, int32_t length);

    __aicore__ inline void StoreDirectSplit(
        NttSplitBuffers& buffers, GM_ADDR* destinations, const DirectSplitPlan& plan);

    __aicore__ inline void DirectSplitBlockHalf(
        GM_ADDR dst0, GM_ADDR dst1, GM_ADDR dst2, GM_ADDR dst3, __gm__ int32_t* srcBase, int32_t srcStride,
        int32_t blockBenchOffset, int32_t actualBench, int32_t logicalN, int32_t kernelN, int32_t paddedBenchWidth);

    __aicore__ inline RowPairWork MakeRowPairWork() const;

    __aicore__ inline void InitRowPairTables(RowPairBuffers& b);

    __aicore__ inline void LoadRowPairTwiddles(RowPairBuffers& b, int32_t rowStart);

    __aicore__ inline void ProcessRowPairHalf(RowPairBuffers& b, size_t polyOffset, int32_t rowStart, int32_t half);

    __aicore__ inline void ProcessRowPairPolynomial(RowPairBuffers& b, int32_t poly, int32_t rowStart);

    __aicore__ inline void PreparePhase2Input64x64RowPairInt10();

    __aicore__ inline PanelWork MakePanelWork() const;

    __aicore__ inline void LoadPhase2Panel(LocalTensor<int32_t>& dst, __gm__ int32_t* base, const PanelRect& rect);

    __aicore__ inline void StorePhase2Panel(LocalTensor<int32_t>& src, size_t polyOffset, const PanelRect& rect);

    __aicore__ inline void SplitPhase2Twiddles(Phase2Buffers& b);

    __aicore__ inline void MultiplyPhase2Panel(Phase2Buffers& b);

    __aicore__ inline void RunPhase2Panel(Phase2Buffers& b, size_t polyOffset, const PanelRect& rect);

    __aicore__ inline void PrepareLocalPanel(Phase2Buffers& b, const PanelWork& work, int32_t localPanel);

    __aicore__ inline void InitTransposeTable(LocalTensor<uint32_t>& lut);

    __aicore__ inline void PreparePhase2Input();

    __aicore__ inline void SyncAllCores();

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

#include "ntt_device_runtime.hpp"
#include "ntt_device_pqc.hpp"
#include "ntt_device_merge.hpp"
#include "ntt_device_subntt.hpp"
#include "ntt_device_phase2.hpp"
