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
#include <array>
#include "../tests/common/ntt_test_utils.h"
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>

#include "acl/acl.h"
#include "aclnn_ntt.h"
#include "../op_kernel/ntt_tiling_data.h"

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
using cann_crypto::ntt::test::PrepackBatMatricesToB1Nz;
using cann_crypto::ntt::test::ReadBinary;

constexpr char kDataDir[] = "ntt_example_data";
constexpr size_t kBatTileCount = 16;
constexpr size_t kPqcN = 256;
constexpr size_t kSystemWorkspaceBytes = 16UL * 1024UL * 1024UL;

std::string Profile()
{
    const char* value = std::getenv("NTT_PROFILE");
    return value == nullptr || *value == '\0' ? "rns" : value;
}

bool IsMlKemProfile() { return Profile() == "ml-kem"; }

bool IsMlDsaProfile() { return Profile() == "ml-dsa"; }

int64_t GetShapeSize(const std::vector<int64_t>& shape)
{
    int64_t shapeSize = 1;
    for (auto i : shape) {
        shapeSize *= i;
    }
    return shapeSize;
}

int Init(int32_t deviceId, aclrtStream* stream)
{
    auto ret = aclInit(nullptr);
    CHECK_RET(ret == ACL_SUCCESS, LOG_PRINT("aclInit failed. ERROR: %d\n", ret); return ret);
    ret = aclrtSetDevice(deviceId);
    CHECK_RET(ret == ACL_SUCCESS, LOG_PRINT("aclrtSetDevice failed. ERROR: %d\n", ret); return ret);
    ret = aclrtCreateStream(stream);
    CHECK_RET(ret == ACL_SUCCESS, LOG_PRINT("aclrtCreateStream failed. ERROR: %d\n", ret); return ret);
    return 0;
}

template <typename T>
int CreateAclTensor(
    const std::vector<T>& hostData, const std::vector<int64_t>& shape, void** deviceAddr, aclDataType dataType,
    aclTensor** tensor)
{
    const size_t size = static_cast<size_t>(GetShapeSize(shape)) * sizeof(T);
    auto ret = aclrtMalloc(deviceAddr, size, ACL_MEM_MALLOC_HUGE_FIRST);
    CHECK_RET(ret == ACL_SUCCESS, LOG_PRINT("aclrtMalloc failed. ERROR: %d\n", ret); return ret);
    ret = aclrtMemcpy(*deviceAddr, size, hostData.data(), size, ACL_MEMCPY_HOST_TO_DEVICE);
    CHECK_RET(ret == ACL_SUCCESS, LOG_PRINT("aclrtMemcpy failed. ERROR: %d\n", ret); return ret);

    std::vector<int64_t> strides(shape.size(), 1);
    for (int64_t i = static_cast<int64_t>(shape.size()) - 2; i >= 0; i--) {
        strides[static_cast<size_t>(i)] = shape[static_cast<size_t>(i + 1)] * strides[static_cast<size_t>(i + 1)];
    }
    *tensor = aclCreateTensor(
        shape.data(), shape.size(), dataType, strides.data(), 0, aclFormat::ACL_FORMAT_ND, shape.data(), shape.size(),
        *deviceAddr);
    return *tensor == nullptr ? -1 : 0;
}

std::string InputPath(const std::string& filename) { return std::string(kDataDir) + "/input/" + filename; }

std::string OutputPath(const std::string& filename) { return std::string(kDataDir) + "/output/" + filename; }

int GenerateData()
{
    // Each invocation runs in its own output directory. Read test assets from
    // the source location supplied by this repository's CMake target.
    try {
        std::filesystem::create_directories(kDataDir);
        for (const auto& entry : std::filesystem::directory_iterator(NTT_DATA_SOURCE_DIR)) {
            if (entry.is_regular_file()) {
                std::filesystem::copy_file(
                    entry.path(), std::filesystem::path(kDataDir) / entry.path().filename(),
                    std::filesystem::copy_options::overwrite_existing);
            }
        }
    } catch (const std::filesystem::filesystem_error& error) {
        LOG_PRINT("prepare NTT test assets failed: %s\n", error.what());
        return -1;
    }
    const std::string command =
        "PY=${NTT_PYTHON:-}; "
        "if [ -z \"$PY\" ] && [ -x /usr/local/python3.11.14/bin/python3 ]; then "
        "PY=/usr/local/python3.11.14/bin/python3; fi; "
        "if [ -z \"$PY\" ]; then PY=python3; fi; "
        "cd ntt_example_data && "
        "PROFILE=${NTT_PROFILE:-rns}; "
        "if [ \"$PROFILE\" = ml-kem ] || [ \"$PROFILE\" = ml-dsa ]; then "
        "if [ \"$PROFILE\" = ml-kem ]; then KERNEL=mlkem-poly; else KERNEL=mldsa-poly; fi; "
        "PQC_KERNEL=$KERNEL PQC_OPERATION=ntt PQC_BATCH=${NTT_BENCH:-1024} "
        "PQC_SEED=${NTT_SEED:-910203} NTT_DATA_DIR=$PWD \"$PY\" gen_pqc_data.py; "
        "else "
        "NTT_N=${NTT_N:-65536} NTT_BLOCK_DIM=${NTT_BLOCK_DIM:-16} NTT_W=${NTT_W:-32} "
        "NTT_BENCH=${NTT_BENCH:-1} NTT_CHANNEL_BLOCK=${NTT_CHANNEL_BLOCK:-1} "
        "NTT_RNS_CHANNELS=${NTT_RNS_CHANNELS:-0} NTT_LOGP=${NTT_LOGP:-200} \"$PY\" gen_data.py; "
        "fi";
    return std::system(command.c_str());
}

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

bool CheckResult(const std::vector<int32_t>& result, const std::vector<int32_t>& golden)
{
    if (result.size() != golden.size()) {
        LOG_PRINT("result size mismatch, actual=%zu, expected=%zu\n", result.size(), golden.size());
        return false;
    }
    for (size_t i = 0; i < result.size(); ++i) {
        const bool same =
            IsMlDsaProfile() ? (static_cast<int64_t>(result[i]) - golden[i]) % 8380417 == 0 : result[i] == golden[i];
        if (!same) {
            LOG_PRINT("result mismatch at %zu: actual=%d, expected=%d\n", i, result[i], golden[i]);
            return false;
        }
    }
    LOG_PRINT("Ntt example result matched golden, size=%zu\n", result.size());
    return true;
}

void SetQuietAscendLogs()
{
    (void)setenv("ASCEND_SLOG_PRINT_TO_STDOUT", "0", 0);
    (void)setenv("ASCEND_GLOBAL_LOG_LEVEL", "3", 0);
    (void)setenv("ASCEND_GLOBAL_EVENT_ENABLE", "0", 0);
}

struct NttExampleData {
    std::vector<int32_t> input, golden, twiddle, output;
    std::vector<int8_t> matrixPhase1, matrixPhase2;
    std::vector<int64_t> xShape, matrixPhase1Shape, matrixPhase2Shape, twiddleShape;
    NttTilingData tiling{};

    int Read()
    {
        std::vector<int8_t> matrixPhase1Raw, matrixPhase2Raw;
        std::vector<NttTilingData> tilingVector;
        CHECK_RET(ReadBinary(InputPath("input_x.bin"), input), return -1);
        CHECK_RET(ReadBinary(OutputPath("golden.bin"), golden), return -1);
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
        return PrepareShapes();
    }

    int PrepareShapes()
    {
        CHECK_RET(tiling.n > 0, return -1);
        CHECK_RET(input.size() % static_cast<size_t>(tiling.n) == 0, return -1);
        const size_t phase1Bytes =
            static_cast<size_t>(tiling.rnsChannels) * kBatTileCount * tiling.phase1KernelN * tiling.phase1KernelN;
        const size_t phase2Bytes =
            static_cast<size_t>(tiling.rnsChannels) * kBatTileCount * tiling.phase2KernelN * tiling.phase2KernelN;
        CHECK_RET(matrixPhase1.size() == phase1Bytes && matrixPhase2.size() == phase2Bytes, return -1);
        output.assign(golden.size(), 0);
        xShape = {static_cast<int64_t>(input.size() / tiling.n), tiling.n};
        matrixPhase1Shape = {static_cast<int64_t>(matrixPhase1.size())};
        matrixPhase2Shape = {static_cast<int64_t>(matrixPhase2.size())};
        twiddleShape = {static_cast<int64_t>(twiddle.size())};
        return 0;
    }
};

class NttExample {
public:
    ~NttExample()
    {
        if (initialized_) {
            Cleanup();
        }
    }

    int Run()
    {
        CHECK_RET(data_.Read() == 0, return -1);
        auto ret = Init(deviceId_, &stream_);
        CHECK_RET(ret == ACL_SUCCESS, return ret);
        initialized_ = true;
        ret = CreateInputs();
        CHECK_RET(ret == ACL_SUCCESS, return ret);
        ret = PrepareExecutor();
        CHECK_RET(ret == ACL_SUCCESS, return ret);
        ret = AllocateWorkspace();
        CHECK_RET(ret == ACL_SUCCESS, return ret);
        ret = Execute();
        CHECK_RET(ret == ACL_SUCCESS, return ret);
        return CheckOutput();
    }

private:
    int CreateInputs()
    {
        std::vector<int64_t> qs, gs;
        for (int32_t i = 0; i < data_.tiling.rnsChannels; ++i) {
            qs.push_back(data_.tiling.qList[i] > 0 ? data_.tiling.qList[i] : data_.tiling.q);
            gs.push_back(data_.tiling.gList[i] > 0 ? data_.tiling.gList[i] : data_.tiling.g);
        }
        qList_ = aclCreateIntArray(qs.data(), qs.size());
        gList_ = aclCreateIntArray(gs.data(), gs.size());
        CHECK_RET(qList_ != nullptr && gList_ != nullptr, return -1);
        auto ret = CreateAclTensor(data_.input, data_.xShape, &device_[0], ACL_INT32, &tensor_[0]);
        CHECK_RET(ret == ACL_SUCCESS, return ret);
        ret = CreateAclTensor(data_.matrixPhase1, data_.matrixPhase1Shape, &device_[1], ACL_INT8, &tensor_[1]);
        CHECK_RET(ret == ACL_SUCCESS, return ret);
        ret = CreateAclTensor(data_.matrixPhase2, data_.matrixPhase2Shape, &device_[2], ACL_INT8, &tensor_[2]);
        CHECK_RET(ret == ACL_SUCCESS, return ret);
        ret = CreateAclTensor(data_.twiddle, data_.twiddleShape, &device_[3], ACL_INT32, &tensor_[3]);
        CHECK_RET(ret == ACL_SUCCESS, return ret);
        return CreateAclTensor(data_.output, data_.xShape, &device_[4], ACL_INT32, &tensor_[4]);
    }

    int PrepareExecutor()
    {
        const auto& t = data_.tiling;
        LOG_PRINT("calling aclnnNttGetWorkspaceSize...\n");
        const auto ret = aclnnNttGetWorkspaceSize(
            tensor_[0], tensor_[1], tensor_[2], tensor_[3], t.blockDim, t.subBench, t.channelBlock, t.q, t.g, t.logP,
            t.rnsChannels, qList_, gList_, tensor_[4], &workspaceSize_, &executor_);
        CHECK_RET(ret == ACL_SUCCESS, LOG_PRINT("aclnnNttGetWorkspaceSize failed. ERROR: %d\n", ret); return ret);
        LOG_PRINT("aclnnNttGetWorkspaceSize done, workspaceSize=%lu\n", workspaceSize_);
        return 0;
    }

    int AllocateWorkspace()
    {
        if (workspaceSize_ > 0) {
            const auto ret = aclrtMalloc(&workspace_, workspaceSize_, ACL_MEM_MALLOC_HUGE_FIRST);
            CHECK_RET(ret == ACL_SUCCESS, return ret);
        }
        if (workspaceSize_ > kSystemWorkspaceBytes) {
            void* userWorkspace = static_cast<uint8_t*>(workspace_) + kSystemWorkspaceBytes;
            const size_t bytes = workspaceSize_ - kSystemWorkspaceBytes;
            return aclrtMemset(userWorkspace, bytes, 0, bytes);
        }
        return 0;
    }

    int Execute()
    {
        LOG_PRINT("calling aclnnNtt...\n");
        auto ret = aclnnNtt(workspace_, workspaceSize_, executor_, stream_);
        CHECK_RET(ret == ACL_SUCCESS, LOG_PRINT("aclnnNtt failed. ERROR: %d\n", ret); return ret);
        LOG_PRINT("synchronizing stream...\n");
        ret = aclrtSynchronizeStream(stream_);
        CHECK_RET(ret == ACL_SUCCESS, LOG_PRINT("aclrtSynchronizeStream failed. ERROR: %d\n", ret); return ret);
        const size_t bytes = data_.output.size() * sizeof(int32_t);
        return aclrtMemcpy(data_.output.data(), bytes, device_[4], bytes, ACL_MEMCPY_DEVICE_TO_HOST);
    }

    int CheckOutput()
    {
        if (IsMlKemProfile()) {
            data_.output = RestoreKemParityPolynomials(data_.output);
            CHECK_RET(!data_.output.empty(), return -1);
        }
        CHECK_RET(CheckResult(data_.output, data_.golden), return -1);
        std::ofstream file(OutputPath("output_z.bin"), std::ios::binary);
        file.write(
            reinterpret_cast<const char*>(data_.output.data()),
            static_cast<std::streamsize>(data_.output.size() * sizeof(int32_t)));
        return file.good() ? 0 : -1;
    }

    void Cleanup()
    {
        for (auto tensor : tensor_) {
            if (tensor != nullptr) {
                aclDestroyTensor(tensor);
            }
        }
        for (auto addr : device_) {
            if (addr != nullptr) {
                aclrtFree(addr);
            }
        }
        if (workspace_ != nullptr) {
            aclrtFree(workspace_);
        }
        if (qList_ != nullptr) {
            aclDestroyIntArray(qList_);
        }
        if (gList_ != nullptr) {
            aclDestroyIntArray(gList_);
        }
        aclrtDestroyStream(stream_);
        aclrtResetDevice(deviceId_);
        aclFinalize();
    }

    NttExampleData data_;
    std::array<void*, 5> device_{};
    std::array<aclTensor*, 5> tensor_{};
    int32_t deviceId_ = 0;
    aclrtStream stream_ = nullptr;
    aclIntArray* qList_ = nullptr;
    aclIntArray* gList_ = nullptr;
    aclOpExecutor* executor_ = nullptr;
    void* workspace_ = nullptr;
    uint64_t workspaceSize_ = 0;
    bool initialized_ = false;
};
} // namespace

int main()
{
    SetQuietAscendLogs();
    CHECK_RET(GenerateData() == 0, LOG_PRINT("generate Ntt example data failed\n"); return -1);
    NttExample example;
    return example.Run();
}
