/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "tikicpulib.h"

#include "../../../op_kernel/ntt.cpp"

namespace {

constexpr char kDataDir[] = "./ntt_data";
constexpr char kSourceDataDir[] = "../../../../experimental/math/ntt/tests/ut/op_kernel/ntt_data";
constexpr size_t kMinGmAllocBytes = 1024;
constexpr int32_t kMaxLaunchBlockDim = 16;

template <typename T>
bool ReadBinary(const std::string& path, std::vector<T>& out)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return false;
    }
    const std::streamsize bytes = file.tellg();
    if (bytes < 0 || bytes % static_cast<std::streamsize>(sizeof(T)) != 0) {
        return false;
    }
    out.resize(static_cast<size_t>(bytes) / sizeof(T));
    file.seekg(0, std::ios::beg);
    return file.read(reinterpret_cast<char*>(out.data()), bytes).good();
}

template <typename T>
bool WriteBinary(const std::string& path, const T* data, size_t elemCount)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }
    file.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(elemCount * sizeof(T)));
    return file.good();
}

int RunCommand(const std::string& command)
{
    return std::system(command.c_str());
}

size_t CeilDiv(size_t a, size_t b)
{
    return (a + b - 1) / b;
}

void PackSingleBatMatrixToB1Nz(
    const std::vector<int8_t>& src, size_t srcOffset, size_t n, std::vector<int8_t>& dst, size_t dstOffset)
{
    constexpr size_t kBlock = 32;
    constexpr size_t nBlock = 32;
    const size_t kBlockCount = CeilDiv(n, kBlock);
    const size_t nBlockCount = CeilDiv(n, nBlock);
    const size_t tileBytes = kBlock * nBlock;

    std::fill(
        dst.begin() + static_cast<std::ptrdiff_t>(dstOffset),
        dst.begin() + static_cast<std::ptrdiff_t>(dstOffset + n * n), static_cast<int8_t>(0));

    for (size_t nb = 0; nb < nBlockCount; ++nb) {
        for (size_t kb = 0; kb < kBlockCount; ++kb) {
            const size_t tileBase = dstOffset + (nb * kBlockCount + kb) * tileBytes;
            for (size_t kr = 0; kr < kBlock; ++kr) {
                const size_t srcRow = kb * kBlock + kr;
                if (srcRow >= n) {
                    break;
                }
                for (size_t nc = 0; nc < nBlock; ++nc) {
                    const size_t srcCol = nb * nBlock + nc;
                    if (srcCol >= n) {
                        break;
                    }
                    dst[tileBase + kr * nBlock + nc] = src[srcOffset + srcRow * n + srcCol];
                }
            }
        }
    }
}

std::vector<int8_t> PrepackBatMatricesToB1Nz(const std::vector<int8_t>& raw, size_t n)
{
    const size_t perMatrixBytes = n * n;
    std::vector<int8_t> packed(raw.size(), 0);
    const size_t matrixCount = raw.size() / perMatrixBytes;
    for (size_t matIdx = 0; matIdx < matrixCount; ++matIdx) {
        const size_t offset = matIdx * perMatrixBytes;
        PackSingleBatMatrixToB1Nz(raw, offset, n, packed, offset);
    }
    return packed;
}

class GmBuffer {
public:
    explicit GmBuffer(size_t bytes) : bytes_(std::max(bytes, kMinGmAllocBytes))
    {
        ptr_ = static_cast<uint8_t*>(AscendC::GmAlloc(bytes_));
        if (ptr_ != nullptr) {
            std::memset(ptr_, 0, bytes_);
        }
    }

    ~GmBuffer()
    {
        if (ptr_ != nullptr) {
            AscendC::GmFree(ptr_);
        }
    }

    GmBuffer(const GmBuffer&) = delete;
    GmBuffer& operator=(const GmBuffer&) = delete;

    uint8_t* Get() const
    {
        return ptr_;
    }

    template <typename T>
    T* As() const
    {
        return reinterpret_cast<T*>(ptr_);
    }

    template <typename T>
    void CopyFrom(const std::vector<T>& src)
    {
        std::memcpy(ptr_, src.data(), src.size() * sizeof(T));
    }

    template <typename T>
    void CopyObject(const T& value)
    {
        std::memcpy(ptr_, &value, sizeof(T));
    }

private:
    size_t bytes_;
    uint8_t* ptr_{nullptr};
};

struct NttCase {
    int32_t n;
    int32_t blockDim;
    int32_t subBench;
    int32_t polyCount;
    int32_t channelBlock;
    int32_t rnsChannels;
    int32_t logP;
};

int32_t CaseRnsChannels(const NttCase& testCase)
{
    return testCase.rnsChannels > 0 ? testCase.rnsChannels : 1;
}

int32_t CaseChannelBlock(const NttCase& testCase)
{
    const int32_t rnsChannels = CaseRnsChannels(testCase);
    const int32_t configured = testCase.channelBlock > 0 ? testCase.channelBlock : 1;
    return configured < rnsChannels ? configured : rnsChannels;
}

int32_t CaseLaunchBlockDim(const NttCase& testCase)
{
    return testCase.blockDim * CaseChannelBlock(testCase);
}

std::string NttCaseName(const NttCase& testCase)
{
    return "N" + std::to_string(testCase.n) + "BlockDim" + std::to_string(testCase.blockDim) + "SubBench" +
           std::to_string(testCase.subBench) + "Poly" + std::to_string(testCase.polyCount) + "ChannelBlock" +
           std::to_string(testCase.channelBlock) + "Rns" + std::to_string(CaseRnsChannels(testCase)) + "LogP" +
           std::to_string(testCase.logP);
}

std::vector<NttCase> NttCases()
{
    return {
        {32, 1, 16, 1, 1, 0, 0},
        {64, 2, 16, 1, 1, 0, 0},
        {256, 4, 16, 1, 1, 0, 0},
        {256, 10, 64, 1, 1, 0, 0},
        {256, 10, 64, 3, 1, 0, 0},
        {256, 8, 64, 1, 2, 2, 0},
        {512, 4, 32, 2, 1, 0, 0},
        {1024, 4, 64, 1, 4, 4, 112},
        {4096, 8, 128, 1, 1, 0, 0},
        {8192, 12, 64, 1, 1, 0, 0},
        {16384, 16, 64, 1, 1, 1, 28},
        {32768, 4, 16, 1, 1, 1, 28},
        {65536, 16, 32, 1, 1, 1, 28},
    };
}

std::string GetParamName(const testing::TestParamInfo<NttCase>& info)
{
    return NttCaseName(info.param);
}

std::string InputPath(const std::string& filename)
{
    return std::string(kDataDir) + "/input/" + filename;
}

std::string OutputPath(const std::string& filename)
{
    return std::string(kDataDir) + "/output/" + filename;
}

std::string BuildGenDataCommand(const NttCase& testCase)
{
    return std::string("cd ") + kDataDir + " && rm -rf input output" + " && NTT_N=" + std::to_string(testCase.n) +
           " NTT_BLOCK_DIM=" + std::to_string(testCase.blockDim) + " NTT_W=" + std::to_string(testCase.subBench) +
           " NTT_BENCH=" + std::to_string(testCase.polyCount) +
           " NTT_CHANNEL_BLOCK=" + std::to_string(testCase.channelBlock) +
           " NTT_RNS_CHANNELS=" + std::to_string(testCase.rnsChannels) + " NTT_LOGP=" + std::to_string(testCase.logP) +
           " python3 gen_data.py";
}

std::string BuildCompareCommand()
{
    return std::string("cd ") + kDataDir + " && python3 compare_data.py output/output.bin output/golden.bin";
}

void RunNttCase(const NttCase& testCase)
{
    SCOPED_TRACE(NttCaseName(testCase));
    ASSERT_LE(CaseLaunchBlockDim(testCase), kMaxLaunchBlockDim);
    ASSERT_EQ(RunCommand(BuildGenDataCommand(testCase)), 0);

    std::vector<int32_t> input;
    std::vector<int32_t> golden;
    std::vector<int32_t> twiddle;
    std::vector<int8_t> matrixPhase1Raw;
    std::vector<int8_t> matrixPhase2Raw;
    std::vector<NttTilingData> tilingVector;
    ASSERT_TRUE(ReadBinary(InputPath("input_x.bin"), input));
    ASSERT_TRUE(ReadBinary(OutputPath("golden.bin"), golden));
    ASSERT_TRUE(ReadBinary(InputPath("twiddle_phase12.bin"), twiddle));
    ASSERT_TRUE(ReadBinary(InputPath("M_phase1.bin"), matrixPhase1Raw));
    ASSERT_TRUE(ReadBinary(InputPath("M_phase2.bin"), matrixPhase2Raw));
    ASSERT_TRUE(ReadBinary(InputPath("input_tiling.bin"), tilingVector));
    ASSERT_EQ(tilingVector.size(), 1U);

    NttTilingData tiling = tilingVector[0];
    std::vector<int8_t> matrixPhase1 = PrepackBatMatricesToB1Nz(matrixPhase1Raw, tiling.phase1KernelN);
    std::vector<int8_t> matrixPhase2 = PrepackBatMatricesToB1Nz(matrixPhase2Raw, tiling.phase2KernelN);
    const FourStepWorkspaceLayout layout = tiling_fourstep::MakeWorkspaceLayout(tiling);

    GmBuffer x(input.size() * sizeof(int32_t));
    GmBuffer z(golden.size() * sizeof(int32_t));
    GmBuffer matrixPhase1Gm(matrixPhase1.size() * sizeof(int8_t));
    GmBuffer matrixPhase2Gm(matrixPhase2.size() * sizeof(int8_t));
    GmBuffer twiddleGm(twiddle.size() * sizeof(int32_t));
    GmBuffer workspace(layout.totalBytes);
    GmBuffer tilingGm(sizeof(NttTilingData));
    ASSERT_NE(x.Get(), nullptr);
    ASSERT_NE(z.Get(), nullptr);
    ASSERT_NE(matrixPhase1Gm.Get(), nullptr);
    ASSERT_NE(matrixPhase2Gm.Get(), nullptr);
    ASSERT_NE(twiddleGm.Get(), nullptr);
    ASSERT_NE(workspace.Get(), nullptr);
    ASSERT_NE(tilingGm.Get(), nullptr);
    x.CopyFrom(input);
    matrixPhase1Gm.CopyFrom(matrixPhase1);
    matrixPhase2Gm.CopyFrom(matrixPhase2);
    twiddleGm.CopyFrom(twiddle);
    tilingGm.CopyObject(tiling);

    AscendC::SetKernelMode(KernelMode::MIX_MODE);
    ICPU_RUN_KF(
        ntt, tiling_one::LaunchBlockDim(tiling), x.Get(), matrixPhase1Gm.Get(), matrixPhase2Gm.Get(), twiddleGm.Get(),
        z.Get(), workspace.Get(), tilingGm.Get());

    ASSERT_TRUE(WriteBinary(OutputPath("output.bin"), z.As<int32_t>(), golden.size()));
    ASSERT_EQ(RunCommand(BuildCompareCommand()), 0);
}

class NttKernelTest : public testing::TestWithParam<NttCase> {
protected:
    static void SetUpTestCase()
    {
        ASSERT_EQ(RunCommand(std::string("rm -rf ") + kDataDir), 0);
        ASSERT_EQ(RunCommand(std::string("cp -rf ") + kSourceDataDir + " ./"), 0);
        ASSERT_EQ(RunCommand(std::string("chmod -R 755 ") + kDataDir), 0);
    }

    static void TearDownTestCase()
    {
        (void)RunCommand(std::string("rm -rf ") + kDataDir);
    }
};

TEST_P(NttKernelTest, CpuGolden)
{
    RunNttCase(GetParam());
}

INSTANTIATE_TEST_SUITE_P(NttCpuGolden, NttKernelTest, testing::ValuesIn(NttCases()), GetParamName);

} // namespace
