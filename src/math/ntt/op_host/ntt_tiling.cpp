/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ntt_internal.h"

#include <algorithm>
#include <limits>

namespace cann_crypto::ntt {
namespace {

NttStatus Fail(NttStatus status, const char* message, const char** error)
{
    if (error != nullptr) {
        *error = message;
    }
    return status;
}

bool FitsInt32(int64_t value)
{
    return value >= std::numeric_limits<int32_t>::min() && value <= std::numeric_limits<int32_t>::max();
}

bool IsPowerOfTwo(int64_t value) { return value > 0 && (value & (value - 1)) == 0; }

// 常用长度的固定分解表，命中时直接返回
bool ChooseTabulatedFactors(int32_t n, int32_t& n1, int32_t& n2)
{
    switch (n) {
        case 4096:
            n1 = 64;
            n2 = 64;
            return true;
        case 8192:
            n1 = 128;
            n2 = 64;
            return true;
        case 16384:
            n1 = 128;
            n2 = 128;
            return true;
        case 32768:
            n1 = 128;
            n2 = 256;
            return true;
        case 65536:
            n1 = 256;
            n2 = 256;
            return true;
        default:
            return false;
    }
}

int32_t FloorSqrt(int32_t n)
{
    int32_t root = 1;
    while ((root + 1) * (root + 1) <= n) {
        ++root;
    }
    return root;
}

// 从 sqrt(n) 向下寻找因子；blocks 非 0 时还要求某一侧因子是 blocks 的整数倍
bool SearchFactors(int32_t n, int32_t blocks, int32_t& n1, int32_t& n2)
{
    for (int32_t i = FloorSqrt(n); i > 1; --i) {
        if (n % i != 0) {
            continue;
        }
        if (blocks != 0 && i % blocks != 0 && (n / i) % blocks != 0) {
            continue;
        }
        n1 = i;
        n2 = n / i;
        return true;
    }
    return false;
}

// Retain the original four-step decomposition and direct PQC routing.
void ChooseFactors(int32_t n, int32_t blocks, int32_t& n1, int32_t& n2)
{
    if (blocks > 0 &&
        (ChooseTabulatedFactors(n, n1, n2) || SearchFactors(n, blocks, n1, n2) || SearchFactors(n, 0, n1, n2))) {
        return;
    }
    n1 = 1;
    n2 = n;
}

size_t WorkspaceBytes(const NttTilingData& t)
{
    const size_t channels = static_cast<size_t>(std::min(t.channelBlock, t.rnsChannels));
    const size_t logicalBytes = static_cast<size_t>(t.polyCount) * t.n * sizeof(int32_t);
    const auto stageBytes = [&](int32_t kernelN) {
        const size_t padded = static_cast<size_t>(tiling_one::PaddedBench(kernelN, t.subBench));
        return (4 * padded * kernelN + 4 * padded * kernelN * sizeof(int32_t)) * t.blockDim * 8;
    };
    size_t scratch = std::max(stageBytes(t.phase1KernelN), stageBytes(t.phase2KernelN));
    const bool directPqc = t.useFourStep == 0 && t.n == 256 && t.rnsChannels == 1 && t.channelBlock == 1 &&
                           t.blockDim == 16 && t.subBench == 16 && (t.q == 3329 || t.q == 8380417);
    if (directPqc) {
        const size_t padded = ((static_cast<size_t>(t.polyCount) + t.subBench - 1) / t.subBench) * t.subBench;
        const size_t planeBytes = padded * t.n;
        scratch = std::max(scratch, 4 * planeBytes + 16 * planeBytes * sizeof(int32_t));
    }
    const size_t sync = static_cast<size_t>(t.blockDim) * 2 * 8 * sizeof(int32_t);
    return (2 * logicalBytes + scratch + sync) * channels;
}

NttStatus ValidateAttributes(const NttParams& p, const char** error)
{
    if (!IsPowerOfTwo(p.n) || p.n > 65536 || p.xElements <= 0) {
        return Fail(NttStatus::InvalidShape, "N must be a power of two in 1..65536 and x must be nonempty", error);
    }
    if (p.qListCount > 16 || p.gListCount > 16 || (p.gListCount != 0 && p.gListCount != p.qListCount)) {
        return Fail(
            NttStatus::InvalidArgument, "q_list/g_list must contain at most 16 entries and their lengths must match",
            error);
    }
    if (!FitsInt32(p.q) || !FitsInt32(p.g) || !FitsInt32(p.logP) || !FitsInt32(p.rnsChannels) ||
        !FitsInt32(p.shapeRnsChannels)) {
        return Fail(NttStatus::InvalidArgument, "integer attribute exceeds the 32-bit kernel representation", error);
    }
    return NttStatus::Success;
}

int64_t ResolveChannels(const NttParams& p)
{
    int64_t channels = p.qListCount != 0 ? static_cast<int64_t>(p.qListCount) : p.rnsChannels;
    if (channels <= 0) {
        channels = p.shapeRnsChannels > 0 ? p.shapeRnsChannels : 1;
    }
    return channels;
}

NttStatus ValidateLaunchShape(const NttParams& p, int64_t channels, const char** error)
{
    if (channels > 16 || p.blockDim <= 0 || p.blockDim > 16 ||
        (p.channelBlock != 1 && p.channelBlock != 2 && p.channelBlock != 4) ||
        p.blockDim * std::min(p.channelBlock, channels) > 16) {
        return Fail(
            NttStatus::InvalidArgument, "RNS channels must be 1..16 and block_dim * active channel_block must be <=16",
            error);
    }
    if (p.subBench != 16 && p.subBench != 32 && p.subBench != 64 && p.subBench != 128) {
        return Fail(NttStatus::InvalidArgument, "sub_bench must be 16, 32, 64, or 128", error);
    }
    return NttStatus::Success;
}

NttTilingData InitialTiling(const NttParams& p, int64_t channels)
{
    NttTilingData t{};
    t.n = static_cast<int32_t>(p.n);
    t.q = static_cast<int32_t>(p.q);
    t.g = static_cast<int32_t>(p.g);
    t.blockDim = static_cast<int32_t>(p.blockDim);
    t.subBench = static_cast<int32_t>(p.subBench);
    t.channelBlock = static_cast<int32_t>(p.channelBlock);
    t.logP = static_cast<int32_t>(p.logP);
    t.rnsChannels = static_cast<int32_t>(channels);
    return t;
}

NttStatus CopyModuli(const NttParams& p, NttTilingData& t, const char** error)
{
    if (p.n <= 0) {
        return Fail(NttStatus::InvalidShape, "N must be positive", error);
    }
    for (size_t i = 0; i < p.qListCount; ++i) {
        if (p.qList[i] <= 1 || p.qList[i] >= (1 << 28) || (p.qList[i] - 1) % p.n != 0) {
            return Fail(
                NttStatus::InvalidArgument, "each q_list entry must be in (1, 2^28) and satisfy q = 1 mod N", error);
        }
        t.qList[i] = static_cast<int32_t>(p.qList[i]);
    }
    for (size_t i = 0; i < p.gListCount; ++i) {
        if (p.gList[i] <= 1 || p.gList[i] >= p.qList[i]) {
            return Fail(NttStatus::InvalidArgument, "each g_list entry must be in (1, q_list[i])", error);
        }
        t.gList[i] = static_cast<int32_t>(p.gList[i]);
    }
    if (p.qListCount != 0) {
        t.q = t.qList[0];
        if (p.gListCount != 0) {
            t.g = t.gList[0];
        }
    } else if (p.q <= 1 || p.q >= (1 << 28) || (p.q - 1) % p.n != 0 || p.g <= 1 || p.g >= p.q) {
        return Fail(NttStatus::InvalidArgument, "q must be in (1, 2^28), q = 1 mod N, and g must be in (1, q)", error);
    }
    return NttStatus::Success;
}

NttStatus SetPolynomialCount(const NttParams& p, NttTilingData& t, const char** error)
{
    const int64_t channels = t.rnsChannels;
    if (channels <= 0 || p.n <= 0) {
        return Fail(NttStatus::InvalidShape, "N and RNS channels must be positive", error);
    }
    if (p.xElements % (channels * p.n) != 0) {
        return Fail(NttStatus::InvalidShape, "x element count must be divisible by rns_channels * N", error);
    }
    // Device phase counters and polynomial indices use int32_t. Check before narrowing.
    const int64_t perChannel = p.xElements / channels;
    const int64_t polys = perChannel / p.n;
    if (perChannel > std::numeric_limits<int32_t>::max() || polys > std::numeric_limits<int32_t>::max() - p.subBench) {
        return Fail(NttStatus::InvalidShape, "per-channel input exceeds the 32-bit device indexing range", error);
    }
    t.polyCount = static_cast<int32_t>(polys);
    return NttStatus::Success;
}

NttStatus SetKernelShape(const NttParams& p, NttTilingData& t, const char** error)
{
    // Generic cyclic NTT tables carry N twiddles per channel. Direct PQC tables
    // carry one; a shared N/modulus alone does not identify their packing.
    const bool directPqc = t.n == 256 && (t.q == 3329 || t.q == 8380417) && p.twiddleElements == t.rnsChannels;
    t.useFourStep = (t.n > 256 || (t.n >= 64 && t.blockDim > 1 && !directPqc)) ? 1 : 0;
    if (t.useFourStep != 0) {
        ChooseFactors(t.n, t.blockDim, t.n1, t.n2);
    } else {
        t.n1 = 1;
        t.n2 = t.n;
    }
    t.phase1KernelN = std::max(32, t.useFourStep != 0 ? t.n2 : t.n);
    t.phase2KernelN = std::max(32, t.useFourStep != 0 ? t.n1 : t.n);
    if (t.phase1KernelN > 256 || t.phase2KernelN > 256) {
        return Fail(NttStatus::InvalidArgument, "phase kernel N must be <=256", error);
    }
    return NttStatus::Success;
}

NttStatus ValidateBufferShapes(const NttParams& p, const NttTilingData& t, const char** error)
{
    const int64_t channels = t.rnsChannels;
    const int64_t matrix1 = channels * 16 * t.phase1KernelN * t.phase1KernelN;
    const int64_t matrix2 = channels * 16 * t.phase2KernelN * t.phase2KernelN;
    const int64_t twiddle = channels * (t.useFourStep != 0 ? p.n : 1);
    if (p.matrixPhase1Elements != matrix1 || p.matrixPhase2Elements != matrix2 || p.twiddleElements != twiddle) {
        return Fail(NttStatus::InvalidShape, "matrix or twiddle element count does not match the chosen tiling", error);
    }
    return NttStatus::Success;
}

} // namespace

NttStatus BuildNttTiling(const NttParams& p, NttTilingData& tiling, size_t& userWorkspaceBytes, const char** error)
{
    static_assert(sizeof(size_t) >= 8, "NTT host requires a 64-bit address space");
    tiling = {};
    userWorkspaceBytes = 0;
    if (error != nullptr) {
        *error = nullptr;
    }
    if (const auto status = ValidateAttributes(p, error); status != NttStatus::Success) {
        return status;
    }
    const int64_t channels = ResolveChannels(p);
    if (const auto status = ValidateLaunchShape(p, channels, error); status != NttStatus::Success) {
        return status;
    }
    NttTilingData t = InitialTiling(p, channels);
    if (const auto status = CopyModuli(p, t, error); status != NttStatus::Success) {
        return status;
    }
    if (const auto status = SetPolynomialCount(p, t, error); status != NttStatus::Success) {
        return status;
    }
    if (const auto status = SetKernelShape(p, t, error); status != NttStatus::Success) {
        return status;
    }
    if (const auto status = ValidateBufferShapes(p, t, error); status != NttStatus::Success) {
        return status;
    }
    userWorkspaceBytes = WorkspaceBytes(t);
    tiling = t;
    return NttStatus::Success;
}
} // namespace cann_crypto::ntt
