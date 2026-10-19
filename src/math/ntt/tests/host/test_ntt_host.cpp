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
#include "data_utils.h"

#include <cstddef>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

using namespace cann_crypto::ntt;
namespace {
int failures = 0;
int checks = 0;
#define CHECK(condition)                                         \
    do {                                                         \
        ++checks;                                                \
        if (!(condition)) {                                      \
            ++failures;                                          \
            std::cerr << __LINE__ << ": " << #condition << '\n'; \
        }                                                        \
    } while (false)

NttParams Pqc(int64_t q)
{
    NttParams p;
    p.n = 256;
    p.xElements = 4096 * 256;
    p.matrixPhase1Elements = 16 * 256 * 256;
    p.matrixPhase2Elements = p.matrixPhase1Elements;
    p.twiddleElements = 1;
    p.q = q;
    p.g = q == 3329 ? 17 : 1753;
    p.blockDim = 16;
    p.subBench = 16;
    return p;
}

void CheckFormalPqc()
{
    for (const int64_t q : {3329, 8380417}) {
        auto p = Pqc(q);
        NttPlan plan;
        const char* error = "stale";
        CHECK(PrepareNttLaunch(p, plan, &error) == NttStatus::Success);
        CHECK(error == nullptr);
        // Frozen expected binary ABI and workspace for the original batch=4096 path.
        NttTilingData expected{};
        expected.n = 256;
        expected.q = static_cast<int32_t>(q);
        expected.g = static_cast<int32_t>(p.g);
        expected.polyCount = 4096;
        expected.blockDim = 16;
        expected.subBench = 16;
        expected.n1 = 1;
        expected.n2 = 256;
        expected.phase1KernelN = 256;
        expected.phase2KernelN = 256;
        expected.rnsChannels = 1;
        expected.channelBlock = 1;
        CHECK(std::memcmp(&plan.tiling, &expected, sizeof(expected)) == 0);
        CHECK(plan.userWorkspaceBytes == 79692800);
        CHECK(plan.workspaceBytes == 79692800);
        CHECK(plan.launchBlockDim == 16);
        CHECK(plan.variant == (q == 3329 ? NttVariant::KemBat2 : NttVariant::DsaBat3));
        p.qListCount = 1;
        p.gListCount = 1;
        p.qList[0] = q;
        p.gList[0] = p.g;
        CHECK(PrepareNttLaunch(p, plan) == NttStatus::Success);
        CHECK(plan.tiling.qList[0] == q && plan.tiling.gList[0] == p.g);
        CHECK(plan.userWorkspaceBytes == 79692800);

        // The same N/q also supports generic cyclic NTT with four-step tables.
        p.matrixPhase1Elements = 16 * 32 * 32;
        p.matrixPhase2Elements = p.matrixPhase1Elements;
        p.twiddleElements = 256;
        CHECK(PrepareNttLaunch(p, plan) == NttStatus::Success);
        CHECK(plan.tiling.useFourStep == 1);
        CHECK(plan.tiling.n1 == 16 && plan.tiling.n2 == 16);
        CHECK(plan.variant == NttVariant::FourStepBat4);
    }
}

void CheckRns()
{
    NttParams p;
    p.n = 65536;
    p.xElements = 8 * 65536;
    p.shapeRnsChannels = 8;
    p.matrixPhase1Elements = 8 * 16 * 256 * 256;
    p.matrixPhase2Elements = p.matrixPhase1Elements;
    p.twiddleElements = 8 * 65536;
    p.blockDim = 16;
    p.subBench = 16;
    p.logP = 200;
    p.qListCount = 8;
    for (auto& q : p.qList) {
        q = 201326593;
    }
    NttPlan plan;
    CHECK(PrepareNttLaunch(p, plan) == NttStatus::Success);
    CHECK(plan.tiling.n1 == 256 && plan.tiling.n2 == 256);
    CHECK(plan.tiling.polyCount == 1 && plan.tiling.rnsChannels == 8);
    CHECK(plan.tiling.logP == 200 && plan.variant == NttVariant::FourStepBat4);
    CHECK(plan.userWorkspaceBytes == 11011072 && plan.workspaceBytes == 11011072);
    CHECK(plan.launchBlockDim == 16 && plan.tiling.channelBlock == 1);
    for (int32_t lanes : {1, 2, 4}) {
        p.channelBlock = lanes;
        p.blockDim = 16 / lanes;
        CHECK(PrepareNttLaunch(p, plan) == NttStatus::Success);
        CHECK(plan.launchBlockDim == 16 && plan.tiling.channelBlock == lanes);
        // Independent workspace layout from the unchanged math/device ABI header.
        CHECK(plan.userWorkspaceBytes == tiling_fourstep::MakeWorkspaceLayout(plan.tiling).totalBytes);
    }
    p.channelBlock = 1;
    p.blockDim = 16;
    p.qListCount = 0;
    p.q = 201326593;
    CHECK(PrepareNttLaunch(p, plan) == NttStatus::Success);
    CHECK(plan.tiling.rnsChannels == 8); // Shape fallback.
    p.rnsChannels = 4;
    p.matrixPhase1Elements /= 2;
    p.matrixPhase2Elements /= 2;
    p.twiddleElements /= 2;
    CHECK(PrepareNttLaunch(p, plan) == NttStatus::Success);
    CHECK(plan.tiling.rnsChannels == 4 && plan.tiling.polyCount == 2); // Explicit override.
}

void CheckDecompositions()
{
    const int32_t rows[][3] = {
        {4096, 64, 64}, {8192, 128, 64}, {16384, 128, 128}, {32768, 128, 256}, {65536, 256, 256}};
    for (const auto& row : rows) {
        NttParams p;
        p.n = row[0];
        p.xElements = p.n;
        p.q = 201326593;
        p.blockDim = 16;
        p.subBench = 16;
        p.matrixPhase1Elements = 16 * row[2] * row[2];
        p.matrixPhase2Elements = 16 * row[1] * row[1];
        p.twiddleElements = p.n;
        NttPlan plan;
        CHECK(PrepareNttLaunch(p, plan) == NttStatus::Success);
        CHECK(plan.tiling.n1 == row[1] && plan.tiling.n2 == row[2]);
        CHECK(plan.userWorkspaceBytes == tiling_fourstep::MakeWorkspaceLayout(plan.tiling).totalBytes);
    }
    for (int32_t n : {1, 2, 4, 8, 16, 32, 64, 128, 256}) {
        NttParams p;
        p.n = n;
        p.xElements = n;
        p.blockDim = 1;
        p.subBench = 16;
        p.matrixPhase1Elements = 16 * (n < 32 ? 32 : n) * (n < 32 ? 32 : n);
        p.matrixPhase2Elements = p.matrixPhase1Elements;
        p.twiddleElements = 1;
        NttPlan plan;
        CHECK(PrepareNttLaunch(p, plan) == NttStatus::Success);
        CHECK(plan.tiling.useFourStep == 0 && plan.variant == NttVariant::DirectBat4);
        CHECK(plan.userWorkspaceBytes == tiling_fourstep::MakeWorkspaceLayout(plan.tiling).totalBytes);
    }
}

// 期望 PrepareNttLaunch 拒绝该参数组合，并清零输出
void Reject(const NttParams& p)
{
    NttPlan plan;
    plan.workspaceBytes = 1;
    const char* error = nullptr;
    CHECK(PrepareNttLaunch(p, plan, &error) != NttStatus::Success);
    CHECK(error != nullptr && plan.workspaceBytes == 0 && plan.launchBlockDim == 0);
}

// 形状与并行度维度的非法取值
void CheckInvalidShapeParams()
{
    const auto valid = Pqc(3329);
    for (int64_t n : {0, -1, 3, 131072}) {
        auto p = valid;
        p.n = n;
        Reject(p);
    }
    for (int64_t blocks : {0, -1, 17}) {
        auto p = valid;
        p.blockDim = blocks;
        Reject(p);
    }
    for (int64_t batch : {0, 15, 256}) {
        auto p = valid;
        p.subBench = batch;
        Reject(p);
    }
    for (int64_t lanes : {0, 3, 8}) {
        auto p = valid;
        p.channelBlock = lanes;
        Reject(p);
    }
    auto p = valid;
    p.shapeRnsChannels = 17;
    Reject(p);
    p = valid;
    p.rnsChannels = 2;
    p.channelBlock = 2;
    Reject(p);
}

// 模数与生成元维度的非法取值
void CheckInvalidModulusParams()
{
    const auto valid = Pqc(3329);
    for (int64_t q : {1LL, 3330LL, (1LL << 28), (1LL << 32) + 3329}) {
        auto p = valid;
        p.q = q;
        Reject(p);
    }
    auto p = valid;
    p.qListCount = 17;
    Reject(p);
    p = valid;
    p.gListCount = 1;
    Reject(p);
    p = valid;
    p.qListCount = 1;
    p.qList[0] = (1LL << 32) + 3329;
    Reject(p);
    p = valid;
    p.qListCount = 1;
    p.qList[0] = 3329;
    p.gListCount = 1;
    p.gList[0] = 1;
    Reject(p);
    p = valid;
    p.g = 1;
    Reject(p);
    p = valid;
    p.g = p.q;
    Reject(p);
}

// 缓冲区元素数维度的非法取值
void CheckInvalidBufferParams()
{
    const auto valid = Pqc(3329);
    auto p = valid;
    p.xElements = 0;
    Reject(p);
    p = valid;
    --p.xElements;
    Reject(p);
    p = valid;
    p.xElements = (1LL << 40);
    Reject(p);
    p = valid;
    p.logP = std::numeric_limits<int64_t>::max();
    Reject(p);
    p = valid;
    ++p.matrixPhase1Elements;
    Reject(p);
    p = valid;
    --p.matrixPhase2Elements;
    Reject(p);
    p = valid;
    p.twiddleElements = 256;
    Reject(p);
}

void CheckInvalidParams()
{
    CheckInvalidShapeParams();
    CheckInvalidModulusParams();
    CheckInvalidBufferParams();
}

struct BackendProbe {
    int calls = 0;
    NttStatus result = NttStatus::Success;
    void* stream = nullptr;
};
NttStatus RecordLaunch(void* context, const NttPlan& plan, const NttBuffers&, void* stream)
{
    auto& probe = *static_cast<BackendProbe*>(context);
    ++probe.calls;
    probe.stream = stream;
    CHECK(plan.launchBlockDim == 16 && plan.variant == NttVariant::KemBat2);
    CHECK(plan.tiling.polyCount == 4096 && plan.workspaceBytes == 79692800);
    return probe.result;
}

void CheckArithmeticAndPackingBoundaries()
{
    CHECK(tiling_one::CeilDiv(7, 0) == 0);
    CHECK(tiling_one::CeilDiv(7, -1) == 0);
    CHECK(tiling_one::AlignUp(7, 0) == 0);
    CHECK(tiling_one::AlignUp(7, -1) == 0);
    CHECK(tiling_one::SplitBenchMultiple(0) == 1);
    using cann_crypto::ntt::utils::PrepackBatMatricesToB1Nz;
    std::vector<int8_t> raw(64 * 64, 0);
    raw[32] = 5;
    raw[32 * 64] = 7;
    raw[33 * 64 + 34] = 9;
    raw[63 * 64 + 63] = 11;
    const auto packed = PrepackBatMatricesToB1Nz(raw, 64);
    CHECK(packed.size() == raw.size());
    if (packed.size() != raw.size()) {
        return;
    }
    CHECK(packed[2048] == 5);
    CHECK(packed[1024] == 7);
    CHECK(packed[3106] == 9);
    CHECK(packed[4095] == 11);
    size_t nonzero = 0;
    for (const auto value : packed) {
        nonzero += value != 0 ? 1 : 0;
    }
    CHECK(nonzero == 4);
    CHECK(PrepackBatMatricesToB1Nz(raw, 0).empty());
    CHECK(PrepackBatMatricesToB1Nz(raw, 1).empty());
    CHECK(PrepackBatMatricesToB1Nz(raw, std::numeric_limits<size_t>::max()).empty());
    raw.pop_back();
    CHECK(PrepackBatMatricesToB1Nz(raw, 64).empty());
}

void CheckLaunchContract()
{
    auto p = Pqc(3329);
    NttPlan plan;
    CHECK(PrepareNttLaunch(p, plan) == NttStatus::Success);
    // Host allocations serve only as opaque addresses for the recording backend;
    // no NPU computation is claimed by this test.
    std::vector<int32_t> x(static_cast<size_t>(p.xElements)), y(x.size());
    std::vector<int8_t> matrix(static_cast<size_t>(p.matrixPhase1Elements));
    std::vector<uint8_t> workspace(plan.workspaceBytes);
    int32_t twiddle = 1;
    NttBuffers b{{x.data(), x.size() * 4},    {matrix.data(), matrix.size()}, {matrix.data(), matrix.size()},
                 {&twiddle, sizeof(twiddle)}, {y.data(), y.size() * 4},       {workspace.data(), workspace.size()}};
    BackendProbe probe;
    int streamToken = 0;
    CHECK(LaunchNtt(p, b, RecordLaunch, &probe, &streamToken) == NttStatus::Success);
    CHECK(probe.calls == 1 && probe.stream == &streamToken);
    CHECK(LaunchNtt(p, b, nullptr, nullptr, nullptr) == NttStatus::BackendUnavailable);
    probe.result = NttStatus::LaunchFailed;
    CHECK(LaunchNtt(p, b, RecordLaunch, &probe, nullptr) == NttStatus::LaunchFailed);
    CHECK(probe.calls == 2);
    auto bad = b;
    --bad.workspace.bytes;
    CHECK(LaunchNtt(p, bad, RecordLaunch, &probe, nullptr) == NttStatus::BufferTooSmall);
    bad = b;
    bad.y = bad.x;
    CHECK(LaunchNtt(p, bad, RecordLaunch, &probe, nullptr) == NttStatus::InvalidArgument);
    bad = b;
    bad.workspace.data = b.y.data;
    CHECK(LaunchNtt(p, bad, RecordLaunch, &probe, nullptr) == NttStatus::InvalidArgument);
    bad = b;
    bad.x.data = nullptr;
    CHECK(LaunchNtt(p, bad, RecordLaunch, &probe, nullptr) == NttStatus::InvalidArgument);
    bad = b;
    bad.x.data = reinterpret_cast<void*>(std::numeric_limits<uintptr_t>::max() - 1);
    CHECK(LaunchNtt(p, bad, RecordLaunch, &probe, nullptr) == NttStatus::InvalidArgument);
    p.n = 3;
    CHECK(LaunchNtt(p, b, RecordLaunch, &probe, nullptr) == NttStatus::InvalidShape);
    CHECK(probe.calls == 2);
}
} // namespace

int main()
{
    static_assert(sizeof(NttTilingData) == 184, "device tiling ABI changed");
    static_assert(offsetof(NttTilingData, channelBlock) == 180, "device tiling offset changed");
    CheckArithmeticAndPackingBoundaries();
    CheckFormalPqc();
    CheckRns();
    CheckDecompositions();
    CheckInvalidParams();
    CheckLaunchContract();
    std::cout << "NTT host checks=" << checks << " failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
