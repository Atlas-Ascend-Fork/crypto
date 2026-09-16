/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef NTT_TILING_DATA_H_
#define NTT_TILING_DATA_H_

#include <cstddef>
#include <cstdint>

struct NttTilingData {
    int32_t n;
    int32_t q;
    int32_t g;
    int32_t polyCount;
    int32_t blockDim;
    int32_t subBench;
    int32_t n1;
    int32_t n2;
    int32_t useFourStep;
    int32_t phase1KernelN;
    int32_t phase2KernelN;
    int32_t rnsChannels;
    int32_t logP;
    int32_t qList[16];
    int32_t gList[16];
    // Number of RNS channels processed concurrently by one kernel launch.
    // Kept at the end so older input_tiling.bin files read as channelBlock=0,
    // which defaults to the original single-channel execution.
    int32_t channelBlock;
};

using TilingData = NttTilingData;

struct CubeTilingData {
    int32_t n;
    int32_t bench;
    int32_t blockDim;
    int32_t reserved;
};

struct TransposeTilingData {
    int32_t n1;
    int32_t n2;
    int32_t q;
    int32_t g;
};

namespace tiling_one {
constexpr size_t max_rns_channels = 16;
constexpr int32_t rns_channel_bits = 28;
constexpr size_t max_n = 256;
constexpr size_t matrix_count = 4;
constexpr size_t split_count = 4;
constexpr size_t accum_count = 4;
constexpr size_t sub_ntt_pipeline_slots = 8;
constexpr size_t sync_ints_per_core = 8;
constexpr int32_t mix_aiv_per_aic = 2;

inline bool IsValidN(int32_t n) { return n > 0 && n <= static_cast<int32_t>(max_n) && (n & (n - 1)) == 0; }

inline bool IsValidKernelN(int32_t n) { return IsValidN(n) && n >= 32; }

inline int32_t KernelN(int32_t logicalN) { return logicalN < 32 ? 32 : logicalN; }

inline int32_t AlignUp(int32_t value, int32_t align)
{
    if (align == 0) {
        return 0;
    }
    if (align < 0) {
        return 0;
    }
    return ((value + align - 1) / align) * align;
}

inline int32_t SplitBenchMultiple(int32_t kernelN)
{
    const int32_t splitBytesPerBench = kernelN / 2;
    if (splitBytesPerBench <= 0) {
        return 1;
    }
    return (splitBytesPerBench >= 32) ? 1 : (32 / splitBytesPerBench);
}

inline int32_t PaddedBench(int32_t kernelN, int32_t logicalBench)
{
    int32_t aligned = AlignUp(logicalBench, SplitBenchMultiple(kernelN));
    return aligned < 4 ? 4 : aligned;
}

inline int32_t CeilDiv(int32_t a, int32_t b)
{
    if (b == 0) {
        return 0;
    }
    if (b < 0) {
        return 0;
    }
    return (a + b - 1) / b;
}

inline int32_t RnsChannels(const TilingData& tiling) { return tiling.rnsChannels > 0 ? tiling.rnsChannels : 1; }

inline int32_t ChannelBlock(const TilingData& tiling)
{
    const int32_t rnsChannels = RnsChannels(tiling);
    const int32_t configured = tiling.channelBlock > 0 ? tiling.channelBlock : 1;
    return configured < rnsChannels ? configured : rnsChannels;
}

inline int32_t ChannelGroupCount(const TilingData& tiling)
{
    return CeilDiv(RnsChannels(tiling), ChannelBlock(tiling));
}

inline int32_t LaunchBlockDim(const TilingData& tiling) { return tiling.blockDim * ChannelBlock(tiling); }

inline int32_t MaxBenchPerBlock(int32_t bench, int32_t w) { return (bench <= 0 || w <= 0) ? 0 : CeilDiv(bench, w); }

inline size_t MatrixBytes(size_t n) { return matrix_count * n * n * sizeof(int8_t); }

inline size_t SplitBytes(size_t n, size_t bench) { return split_count * n * bench * sizeof(int8_t); }

inline size_t AccumulatorBytes(size_t n, size_t bench) { return accum_count * n * bench * sizeof(int32_t); }

inline size_t ScratchBytes(size_t n, size_t bench) { return SplitBytes(n, bench) + AccumulatorBytes(n, bench); }

inline size_t WorkspaceBytes(size_t n, size_t maxBenchPerBlock, size_t w)
{
    return MatrixBytes(n) + ScratchBytes(n, maxBenchPerBlock) * w * sub_ntt_pipeline_slots;
}
} // namespace tiling_one

struct FourStepWorkspaceLayout {
    size_t workPhase1Offset;
    size_t workPhase2Offset;
    size_t packedOffset;
    size_t scratchOffset;
    size_t syncOffset;
    size_t totalBytes;
};

namespace tiling_fourstep {
inline size_t Int32Bytes(int32_t elemCount) { return static_cast<size_t>(elemCount) * sizeof(int32_t); }

inline size_t PackedBytes(int32_t kernelN, int32_t subBench, int32_t blockDim)
{
    (void)kernelN;
    (void)subBench;
    (void)blockDim;
    return 0;
}

inline size_t ScratchBytes(int32_t kernelN, int32_t subBench, int32_t blockDim)
{
    return tiling_one::ScratchBytes(kernelN, tiling_one::PaddedBench(kernelN, subBench)) *
           static_cast<size_t>(blockDim) * tiling_one::sub_ntt_pipeline_slots;
}

inline size_t SyncBytes(int32_t blockDim)
{
    return static_cast<size_t>(blockDim) * static_cast<size_t>(tiling_one::mix_aiv_per_aic) *
           tiling_one::sync_ints_per_core * sizeof(int32_t);
}

inline size_t MaxSize(size_t a, size_t b) { return a > b ? a : b; }

inline FourStepWorkspaceLayout MakeWorkspaceLayout(const TilingData& tiling)
{
    // The NTT kernel processes channelBlock RNS channels in parallel and
    // reuses these lane buffers across channel groups. x/z/table buffers
    // remain channel-major.
    const size_t channelBlock = static_cast<size_t>(tiling_one::ChannelBlock(tiling));
    const size_t logicalBytes = static_cast<size_t>(tiling.polyCount) * static_cast<size_t>(tiling.n) * sizeof(int32_t);
    const size_t packedBytes = MaxSize(
        PackedBytes(tiling.phase1KernelN, tiling.subBench, tiling.blockDim),
        PackedBytes(tiling.phase2KernelN, tiling.subBench, tiling.blockDim));
    const size_t scratchBytes = MaxSize(
        ScratchBytes(tiling.phase1KernelN, tiling.subBench, tiling.blockDim),
        ScratchBytes(tiling.phase2KernelN, tiling.subBench, tiling.blockDim));

    FourStepWorkspaceLayout layout{};
    layout.workPhase1Offset = 0;
    layout.workPhase2Offset = layout.workPhase1Offset + logicalBytes * channelBlock;
    layout.packedOffset = layout.workPhase2Offset + logicalBytes * channelBlock;
    layout.scratchOffset = layout.packedOffset + packedBytes * channelBlock;
    layout.syncOffset = layout.scratchOffset + scratchBytes * channelBlock;
    layout.totalBytes = layout.syncOffset + SyncBytes(tiling.blockDim) * channelBlock;
    return layout;
}
} // namespace tiling_fourstep

#endif
