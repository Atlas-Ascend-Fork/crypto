/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "acl/acl.h"
#include "data_utils.h"
#include "ntt_internal.h"

#define CHECK_RET(cond, return_expr) \
    do {                             \
        if (!(cond)) {               \
            return_expr;             \
        }                            \
    } while (0)

#define LOG_PRINT(message, ...)         \
    do {                                \
        printf(message, ##__VA_ARGS__); \
    } while (0)

namespace {
using cann_crypto::ntt::utils::PrepackBatMatricesToB1Nz;
using cann_crypto::ntt::utils::ReadBinary;

constexpr size_t kBatTileCount = 16;
constexpr size_t kPqcN = 256;

std::string Profile()
{
    const char* value = std::getenv("NTT_PROFILE");
    return value == nullptr || *value == '\0' ? "rns" : value;
}

bool IsMlKemProfile() { return Profile() == "ml-kem"; }

template <typename T>
int CreateDeviceBuffer(const std::vector<T>& hostData, void** deviceAddr)
{
    const size_t bytes = hostData.size() * sizeof(T);
    auto ret = aclrtMalloc(deviceAddr, bytes, ACL_MEM_MALLOC_HUGE_FIRST);
    CHECK_RET(ret == ACL_SUCCESS, LOG_PRINT("aclrtMalloc failed. ERROR: %d\n", ret); return ret);
    return aclrtMemcpy(*deviceAddr, bytes, hostData.data(), bytes, ACL_MEMCPY_HOST_TO_DEVICE);
}

std::string InputPath(const std::string& filename) { return "input/" + filename; }

std::string OutputPath(const std::string& filename) { return "output/" + filename; }

std::vector<int32_t> PackKemParityPolynomials(const std::vector<int32_t>& input)
{
    CHECK_RET(input.size() % kPqcN == 0, return {});
    const size_t polyCount = input.size() / kPqcN;
    const size_t halfN = kPqcN / 2;
    std::vector<int32_t> packed(input.size(), 0);
    for (size_t poly = 0; poly < polyCount; ++poly) {
        for (size_t slot = 0; slot < halfN; ++slot) {
            packed[poly * halfN + slot] = input[poly * kPqcN + 2 * slot];
            packed[polyCount * halfN + poly * halfN + slot] = input[poly * kPqcN + 2 * slot + 1];
        }
    }
    return packed;
}

std::vector<int32_t> RestoreKemParityPolynomials(const std::vector<int32_t>& packed)
{
    CHECK_RET(packed.size() % kPqcN == 0, return {});
    const size_t polyCount = packed.size() / kPqcN;
    const size_t halfN = kPqcN / 2;
    std::vector<int32_t> output(packed.size(), 0);
    for (size_t poly = 0; poly < polyCount; ++poly) {
        for (size_t slot = 0; slot < halfN; ++slot) {
            output[poly * kPqcN + 2 * slot] = packed[poly * halfN + slot];
            output[poly * kPqcN + 2 * slot + 1] = packed[polyCount * halfN + poly * halfN + slot];
        }
    }
    return output;
}

void SetQuietAscendLogs()
{
    (void)setenv("ASCEND_SLOG_PRINT_TO_STDOUT", "0", 0);
    (void)setenv("ASCEND_GLOBAL_LOG_LEVEL", "3", 0);
    (void)setenv("ASCEND_GLOBAL_EVENT_ENABLE", "0", 0);
}

struct NttExampleData {
    std::vector<int32_t> input, twiddle, output;
    std::vector<int8_t> matrixPhase1, matrixPhase2;
    NttTilingData tiling{};

    int Read()
    {
        std::vector<int8_t> matrixPhase1Raw, matrixPhase2Raw;
        std::vector<NttTilingData> tilingVector;
        CHECK_RET(ReadBinary(InputPath("input_x.bin"), input), return -1);
        CHECK_RET(ReadBinary(InputPath("twiddle_phase12.bin"), twiddle), return -1);
        CHECK_RET(ReadBinary(InputPath("M_phase1.bin"), matrixPhase1Raw), return -1);
        CHECK_RET(ReadBinary(InputPath("M_phase2.bin"), matrixPhase2Raw), return -1);
        CHECK_RET(ReadBinary(InputPath("input_tiling.bin"), tilingVector) && tilingVector.size() == 1, return -1);
        tiling = tilingVector[0];
        if (IsMlKemProfile()) {
            input = PackKemParityPolynomials(input);
            CHECK_RET(!input.empty(), return -1);
        }
        matrixPhase1 = PrepackBatMatricesToB1Nz(matrixPhase1Raw, tiling.phase1KernelN);
        matrixPhase2 = PrepackBatMatricesToB1Nz(matrixPhase2Raw, tiling.phase2KernelN);
        return ValidateData();
    }

    int ValidateData()
    {
        CHECK_RET(tiling.n > 0, return -1);
        CHECK_RET(input.size() % static_cast<size_t>(tiling.n) == 0, return -1);
        const size_t phase1Bytes =
            static_cast<size_t>(tiling.rnsChannels) * kBatTileCount * tiling.phase1KernelN * tiling.phase1KernelN;
        const size_t phase2Bytes =
            static_cast<size_t>(tiling.rnsChannels) * kBatTileCount * tiling.phase2KernelN * tiling.phase2KernelN;
        CHECK_RET(matrixPhase1.size() == phase1Bytes && matrixPhase2.size() == phase2Bytes, return -1);
        output.assign(input.size(), 0);
        return 0;
    }
};

class NttExample {
public:
    ~NttExample() { Cleanup(); }

    int Run()
    {
        CHECK_RET(data_.Read() == 0, return -1);
        CHECK_RET(PreparePlan() == 0, return -1);
        auto ret = aclInit(nullptr);
        CHECK_RET(ret == ACL_SUCCESS, return ret);
        initialized_ = true;
        ret = aclrtSetDevice(deviceId_);
        CHECK_RET(ret == ACL_SUCCESS, return ret);
        deviceSelected_ = true;
        ret = aclrtCreateStream(&stream_);
        CHECK_RET(ret == ACL_SUCCESS, return ret);
        ret = CreateInputs();
        CHECK_RET(ret == ACL_SUCCESS, return ret);
        ret = aclrtMalloc(&workspace_, plan_.workspaceBytes, ACL_MEM_MALLOC_HUGE_FIRST);
        CHECK_RET(ret == ACL_SUCCESS, return ret);
        ret = aclrtMemset(workspace_, plan_.workspaceBytes, 0, plan_.workspaceBytes);
        CHECK_RET(ret == ACL_SUCCESS, return ret);
        ret = Execute();
        CHECK_RET(ret == ACL_SUCCESS, return ret);
        return WriteOutput();
    }

private:
    int PreparePlan()
    {
        const auto& t = data_.tiling;
        CHECK_RET(t.rnsChannels > 0 && t.rnsChannels <= 16, return -1);
        params_.n = t.n;
        params_.xElements = static_cast<int64_t>(data_.input.size());
        params_.matrixPhase1Elements = static_cast<int64_t>(data_.matrixPhase1.size());
        params_.matrixPhase2Elements = static_cast<int64_t>(data_.matrixPhase2.size());
        params_.twiddleElements = static_cast<int64_t>(data_.twiddle.size());
        params_.q = t.q;
        params_.g = t.g;
        params_.logP = t.logP;
        params_.blockDim = t.blockDim;
        params_.subBench = t.subBench;
        params_.channelBlock = t.channelBlock;
        params_.rnsChannels = t.rnsChannels;
        params_.qListCount = static_cast<size_t>(t.rnsChannels);
        params_.gListCount = params_.qListCount;
        for (int32_t i = 0; i < t.rnsChannels; ++i) {
            params_.qList[i] = t.qList[i] > 0 ? t.qList[i] : t.q;
            params_.gList[i] = t.gList[i] > 0 ? t.gList[i] : t.g;
        }
        const char* error = nullptr;
        const auto status = cann_crypto::ntt::PrepareNttLaunch(params_, plan_, &error);
        CHECK_RET(
            status == cann_crypto::ntt::NttStatus::Success, LOG_PRINT("NTT tiling failed: %s\n", error); return -1);
        LOG_PRINT(
            "[NTT direct] N=%d q=%d batch=%d blockDim=%u subBench=%d workspace=%zu\n", plan_.tiling.n, plan_.tiling.q,
            plan_.tiling.polyCount, plan_.launchBlockDim, plan_.tiling.subBench, plan_.workspaceBytes);
        return 0;
    }

    int CreateInputs()
    {
        auto ret = CreateDeviceBuffer(data_.input, &device_[0]);
        CHECK_RET(ret == ACL_SUCCESS, return ret);
        ret = CreateDeviceBuffer(data_.matrixPhase1, &device_[1]);
        CHECK_RET(ret == ACL_SUCCESS, return ret);
        ret = CreateDeviceBuffer(data_.matrixPhase2, &device_[2]);
        CHECK_RET(ret == ACL_SUCCESS, return ret);
        ret = CreateDeviceBuffer(data_.twiddle, &device_[3]);
        CHECK_RET(ret == ACL_SUCCESS, return ret);
        return CreateDeviceBuffer(data_.output, &device_[4]);
    }

    int Execute()
    {
        using namespace cann_crypto::ntt;
        const NttBuffers buffers{
            {device_[0], data_.input.size() * sizeof(int32_t)},
            {device_[1], data_.matrixPhase1.size()},
            {device_[2], data_.matrixPhase2.size()},
            {device_[3], data_.twiddle.size() * sizeof(int32_t)},
            {device_[4], data_.output.size() * sizeof(int32_t)},
            {workspace_, plan_.workspaceBytes}};
        const char* error = nullptr;
        LOG_PRINT("calling ACLRT_LAUNCH_KERNEL(ntt)...\n");
        const auto status = LaunchNtt(params_, buffers, stream_, &error);
        CHECK_RET(status == NttStatus::Success, LOG_PRINT("NTT launch failed: %s\n", error); return -1);
        const auto ret = aclrtSynchronizeStream(stream_);
        CHECK_RET(ret == ACL_SUCCESS, LOG_PRINT("stream synchronization failed: %d\n", ret); return ret);
        const size_t bytes = data_.output.size() * sizeof(int32_t);
        return aclrtMemcpy(data_.output.data(), bytes, device_[4], bytes, ACL_MEMCPY_DEVICE_TO_HOST);
    }

    int WriteOutput()
    {
        if (IsMlKemProfile()) {
            data_.output = RestoreKemParityPolynomials(data_.output);
            CHECK_RET(!data_.output.empty(), return -1);
        }
        std::ofstream file(OutputPath("output_z.bin"), std::ios::binary);
        file.write(
            reinterpret_cast<const char*>(data_.output.data()),
            static_cast<std::streamsize>(data_.output.size() * sizeof(int32_t)));
        file.close();
        CHECK_RET(file.good(), LOG_PRINT("write output_z.bin failed\n"); return -1);
        LOG_PRINT("Ntt output written, size=%zu\n", data_.output.size());
        return 0;
    }

    void Cleanup()
    {
        if (stream_ != nullptr) {
            (void)aclrtSynchronizeStream(stream_);
        }
        for (auto addr : device_) {
            if (addr != nullptr) {
                (void)aclrtFree(addr);
            }
        }
        if (workspace_ != nullptr) {
            (void)aclrtFree(workspace_);
        }
        if (stream_ != nullptr) {
            (void)aclrtDestroyStream(stream_);
        }
        if (deviceSelected_) {
            (void)aclrtResetDevice(deviceId_);
        }
        if (initialized_) {
            (void)aclFinalize();
        }
    }

    NttExampleData data_;
    cann_crypto::ntt::NttParams params_;
    cann_crypto::ntt::NttPlan plan_;
    std::array<void*, 5> device_{};
    int32_t deviceId_ = 0;
    aclrtStream stream_ = nullptr;
    void* workspace_ = nullptr;
    bool initialized_ = false;
    bool deviceSelected_ = false;
};
} // namespace

int main()
{
    SetQuietAscendLogs();
    NttExample example;
    return example.Run();
}
