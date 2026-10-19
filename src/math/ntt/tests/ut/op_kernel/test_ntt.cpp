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
#include "securec.h"
#include "../../../data_utils.h"
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "tikicpulib.h"

#include "../../../op_kernel/ntt.cpp"

namespace {
using cann_crypto::ntt::utils::PrepackBatMatricesToB1Nz;
using cann_crypto::ntt::utils::ReadBinary;

constexpr char kDataDir[] = "./ntt_data";
const std::string kSourceDataDir =
    (std::filesystem::path(__FILE__).parent_path() / "../../../scripts").lexically_normal().string();
constexpr size_t kMinGmAllocBytes = 1024;
constexpr int32_t kMaxLaunchBlockDim = 16;

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

int RunCommand(const std::string& command) { return std::system(command.c_str()); }

class GmBuffer {
public:
    explicit GmBuffer(size_t bytes) : bytes_(std::max(bytes, kMinGmAllocBytes))
    {
        ptr_ = static_cast<uint8_t*>(AscendC::GmAlloc(bytes_));
        if (ptr_ != nullptr) {
            if (memset_s(ptr_, bytes_, 0, bytes_) != EOK) {
                AscendC::GmFree(ptr_);
                ptr_ = nullptr;
            }
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

    uint8_t* Get() const { return ptr_; }

    template <typename T>
    T* As() const
    {
        return reinterpret_cast<T*>(ptr_);
    }

    template <typename T>
    void CopyFrom(const std::vector<T>& src)
    {
        ASSERT_NE(ptr_, nullptr);
        ASSERT_LE(src.size(), bytes_ / sizeof(T));
        ASSERT_EQ(memcpy_s(ptr_, bytes_, src.data(), src.size() * sizeof(T)), EOK);
    }

    template <typename T>
    void CopyObject(const T& value)
    {
        ASSERT_NE(ptr_, nullptr);
        ASSERT_LE(sizeof(T), bytes_);
        ASSERT_EQ(memcpy_s(ptr_, bytes_, &value, sizeof(T)), EOK);
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

int32_t CaseRnsChannels(const NttCase& testCase) { return testCase.rnsChannels > 0 ? testCase.rnsChannels : 1; }

int32_t CaseChannelBlock(const NttCase& testCase)
{
    const int32_t rnsChannels = CaseRnsChannels(testCase);
    const int32_t configured = testCase.channelBlock > 0 ? testCase.channelBlock : 1;
    return configured < rnsChannels ? configured : rnsChannels;
}

int32_t CaseLaunchBlockDim(const NttCase& testCase) { return testCase.blockDim * CaseChannelBlock(testCase); }

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
        {32, 1, 16, 1, 1, 0, 0},      {64, 2, 16, 1, 1, 0, 0},      {256, 4, 16, 1, 1, 0, 0},
        {256, 10, 64, 1, 1, 0, 0},    {256, 10, 64, 3, 1, 0, 0},    {256, 8, 64, 1, 2, 2, 0},
        {512, 4, 32, 2, 1, 0, 0},     {1024, 4, 64, 1, 4, 4, 112},  {4096, 8, 128, 1, 1, 0, 0},
        {8192, 12, 64, 1, 1, 0, 0},   {16384, 16, 64, 1, 1, 1, 28}, {32768, 4, 16, 1, 1, 1, 28},
        {65536, 16, 32, 1, 1, 1, 28},
    };
}

std::string GetParamName(const testing::TestParamInfo<NttCase>& info) { return NttCaseName(info.param); }

std::string InputPath(const std::string& filename) { return std::string(kDataDir) + "/input/" + filename; }

std::string OutputPath(const std::string& filename) { return std::string(kDataDir) + "/output/" + filename; }

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
    ASSERT_NE(x.Get(), nullptr);
    ASSERT_NE(z.Get(), nullptr);
    ASSERT_NE(matrixPhase1Gm.Get(), nullptr);
    ASSERT_NE(matrixPhase2Gm.Get(), nullptr);
    ASSERT_NE(twiddleGm.Get(), nullptr);
    ASSERT_NE(workspace.Get(), nullptr);
    x.CopyFrom(input);
    matrixPhase1Gm.CopyFrom(matrixPhase1);
    matrixPhase2Gm.CopyFrom(matrixPhase2);
    twiddleGm.CopyFrom(twiddle);

    AscendC::SetKernelMode(KernelMode::MIX_MODE);
    ICPU_RUN_KF(
        ntt, tiling_one::LaunchBlockDim(tiling), x.Get(), matrixPhase1Gm.Get(), matrixPhase2Gm.Get(), twiddleGm.Get(),
        z.Get(), workspace.Get(), tiling);

    ASSERT_TRUE(WriteBinary(OutputPath("output.bin"), z.As<int32_t>(), golden.size()));
    ASSERT_EQ(RunCommand(BuildCompareCommand()), 0);
}

class NttKernelTest : public testing::TestWithParam<NttCase> {
protected:
    static void SetUpTestCase()
    {
        ASSERT_EQ(RunCommand(std::string("rm -rf ") + kDataDir), 0);
        ASSERT_EQ(RunCommand(std::string("cp -rf '") + kSourceDataDir + "' " + kDataDir), 0);
        ASSERT_EQ(RunCommand(std::string("chmod -R 755 ") + kDataDir), 0);
    }

    static void TearDownTestCase() { (void)RunCommand(std::string("rm -rf ") + kDataDir); }
};

TEST(NttTilingCopy, ValidAndNullSource)
{
    NttTilingData source{};
    source.n = 256;
    source.q = 3329;
    source.channelBlock = 1;
    NttTilingData destination{};
    ASSERT_TRUE(ReadCpuTiling(destination, &source));
    EXPECT_EQ(destination.n, source.n);
    EXPECT_EQ(destination.q, source.q);
    EXPECT_EQ(destination.channelBlock, source.channelBlock);
    EXPECT_FALSE(ReadCpuTiling(destination, nullptr));
}

TEST_P(NttKernelTest, CpuGolden) { RunNttCase(GetParam()); }

INSTANTIATE_TEST_SUITE_P(NttCpuGolden, NttKernelTest, testing::ValuesIn(NttCases()), GetParamName);

} // namespace
