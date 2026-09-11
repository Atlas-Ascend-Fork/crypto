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

constexpr char kDataDir[] = "ntt_example_data";
constexpr size_t kBatTileCount = 16;
constexpr size_t kPqcN = 256;
constexpr size_t kSystemWorkspaceBytes = 16UL * 1024UL * 1024UL;

std::string Profile()
{
    const char* value = std::getenv("NTT_PROFILE");
    return value == nullptr || *value == '\0' ? "rns" : value;
}

bool IsMlKemProfile()
{
    return Profile() == "ml-kem";
}

bool IsMlDsaProfile()
{
    return Profile() == "ml-dsa";
}

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
bool ReadBinary(const std::string& path, std::vector<T>& out)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        LOG_PRINT("open file failed: %s\n", path.c_str());
        return false;
    }
    const std::streamsize bytes = file.tellg();
    if (bytes < 0 || bytes % static_cast<std::streamsize>(sizeof(T)) != 0) {
        LOG_PRINT("invalid file size: %s\n", path.c_str());
        return false;
    }
    out.resize(static_cast<size_t>(bytes) / sizeof(T));
    file.seekg(0, std::ios::beg);
    return file.read(reinterpret_cast<char*>(out.data()), bytes).good();
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

    std::fill(dst.begin() + static_cast<std::ptrdiff_t>(dstOffset),
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

std::string InputPath(const std::string& filename)
{
    return std::string(kDataDir) + "/input/" + filename;
}

std::string OutputPath(const std::string& filename)
{
    return std::string(kDataDir) + "/output/" + filename;
}

int GenerateData()
{
    // Each invocation runs in its own output directory. Read test assets from
    // the source location supplied by this repository's CMake target.
    try {
        std::filesystem::create_directories(kDataDir);
        for (const auto& entry : std::filesystem::directory_iterator(NTT_DATA_SOURCE_DIR)) {
            if (entry.is_regular_file()) {
                std::filesystem::copy_file(entry.path(), std::filesystem::path(kDataDir) / entry.path().filename(),
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
        const bool same = IsMlDsaProfile() ?
            (static_cast<int64_t>(result[i]) - golden[i]) % 8380417 == 0 : result[i] == golden[i];
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

} // namespace

int main()
{
    SetQuietAscendLogs();
    CHECK_RET(GenerateData() == 0, LOG_PRINT("generate Ntt example data failed\n"); return -1);

    std::vector<int32_t> input;
    std::vector<int32_t> golden;
    std::vector<int32_t> twiddle;
    std::vector<int8_t> matrixPhase1Raw;
    std::vector<int8_t> matrixPhase2Raw;
    std::vector<NttTilingData> tilingVector;
    CHECK_RET(ReadBinary(InputPath("input_x.bin"), input), return -1);
    CHECK_RET(ReadBinary(OutputPath("golden.bin"), golden), return -1);
    CHECK_RET(ReadBinary(InputPath("twiddle_phase12.bin"), twiddle), return -1);
    CHECK_RET(ReadBinary(InputPath("M_phase1.bin"), matrixPhase1Raw), return -1);
    CHECK_RET(ReadBinary(InputPath("M_phase2.bin"), matrixPhase2Raw), return -1);
    CHECK_RET(ReadBinary(InputPath("input_tiling.bin"), tilingVector) && tilingVector.size() == 1, return -1);
    const NttTilingData tiling = tilingVector[0];
    if (IsMlKemProfile()) {
        input = PackKemParityPolynomials(input);
        CHECK_RET(!input.empty(), return -1);
    }

    std::vector<int8_t> matrixPhase1 =
        PrepackBatMatricesToB1Nz(matrixPhase1Raw, static_cast<size_t>(tiling.phase1KernelN));
    std::vector<int8_t> matrixPhase2 =
        PrepackBatMatricesToB1Nz(matrixPhase2Raw, static_cast<size_t>(tiling.phase2KernelN));
    const size_t phase1Bytes = static_cast<size_t>(tiling.rnsChannels) * kBatTileCount *
        static_cast<size_t>(tiling.phase1KernelN) * static_cast<size_t>(tiling.phase1KernelN);
    const size_t phase2Bytes = static_cast<size_t>(tiling.rnsChannels) * kBatTileCount *
        static_cast<size_t>(tiling.phase2KernelN) * static_cast<size_t>(tiling.phase2KernelN);
    CHECK_RET(matrixPhase1.size() == phase1Bytes, return -1);
    CHECK_RET(matrixPhase2.size() == phase2Bytes, return -1);

    int32_t deviceId = 0;
    aclrtStream stream;
    auto ret = Init(deviceId, &stream);
    CHECK_RET(ret == ACL_SUCCESS, LOG_PRINT("Init acl failed. ERROR: %d\n", ret); return ret);

    std::vector<int32_t> output(golden.size(), 0);
    CHECK_RET(tiling.n > 0 && input.size() % static_cast<size_t>(tiling.n) == 0, return -1);
    const std::vector<int64_t> xShape = {
        static_cast<int64_t>(input.size() / static_cast<size_t>(tiling.n)), static_cast<int64_t>(tiling.n)};
    const std::vector<int64_t> matrixPhase1Shape = {static_cast<int64_t>(matrixPhase1.size())};
    const std::vector<int64_t> matrixPhase2Shape = {static_cast<int64_t>(matrixPhase2.size())};
    const std::vector<int64_t> twiddleShape = {static_cast<int64_t>(twiddle.size())};

    void* xDeviceAddr = nullptr;
    void* matrixPhase1DeviceAddr = nullptr;
    void* matrixPhase2DeviceAddr = nullptr;
    void* twiddleDeviceAddr = nullptr;
    void* outDeviceAddr = nullptr;
    aclTensor* x = nullptr;
    aclTensor* matrixPhase1Tensor = nullptr;
    aclTensor* matrixPhase2Tensor = nullptr;
    aclTensor* twiddleTensor = nullptr;
    aclTensor* out = nullptr;
    std::vector<int64_t> qListData;
    std::vector<int64_t> gListData;
    for (int32_t i = 0; i < tiling.rnsChannels; ++i) {
        qListData.push_back(tiling.qList[i] > 0 ? tiling.qList[i] : tiling.q);
        gListData.push_back(tiling.gList[i] > 0 ? tiling.gList[i] : tiling.g);
    }
    aclIntArray* qList = aclCreateIntArray(qListData.data(), qListData.size());
    aclIntArray* gList = aclCreateIntArray(gListData.data(), gListData.size());
    CHECK_RET(qList != nullptr && gList != nullptr, return -1);

    ret = CreateAclTensor(input, xShape, &xDeviceAddr, aclDataType::ACL_INT32, &x);
    CHECK_RET(ret == ACL_SUCCESS, return ret);
    ret = CreateAclTensor(matrixPhase1, matrixPhase1Shape, &matrixPhase1DeviceAddr, aclDataType::ACL_INT8,
        &matrixPhase1Tensor);
    CHECK_RET(ret == ACL_SUCCESS, return ret);
    ret = CreateAclTensor(matrixPhase2, matrixPhase2Shape, &matrixPhase2DeviceAddr, aclDataType::ACL_INT8,
        &matrixPhase2Tensor);
    CHECK_RET(ret == ACL_SUCCESS, return ret);
    ret = CreateAclTensor(twiddle, twiddleShape, &twiddleDeviceAddr, aclDataType::ACL_INT32, &twiddleTensor);
    CHECK_RET(ret == ACL_SUCCESS, return ret);
    ret = CreateAclTensor(output, xShape, &outDeviceAddr, aclDataType::ACL_INT32, &out);
    CHECK_RET(ret == ACL_SUCCESS, return ret);

    uint64_t workspaceSize = 0;
    aclOpExecutor* executor = nullptr;
    LOG_PRINT("calling aclnnNttGetWorkspaceSize...\n");
    ret = aclnnNttGetWorkspaceSize(x, matrixPhase1Tensor, matrixPhase2Tensor, twiddleTensor, tiling.blockDim,
        tiling.subBench, tiling.channelBlock, tiling.q, tiling.g, tiling.logP, tiling.rnsChannels, qList, gList, out,
        &workspaceSize, &executor);
    CHECK_RET(ret == ACL_SUCCESS, LOG_PRINT("aclnnNttGetWorkspaceSize failed. ERROR: %d\n", ret); return ret);
    LOG_PRINT("aclnnNttGetWorkspaceSize done, workspaceSize=%lu\n", workspaceSize);

    void* workspaceAddr = nullptr;
    if (workspaceSize > 0) {
        ret = aclrtMalloc(&workspaceAddr, workspaceSize, ACL_MEM_MALLOC_HUGE_FIRST);
        CHECK_RET(ret == ACL_SUCCESS, LOG_PRINT("allocate workspace failed. ERROR: %d\n", ret); return ret);
    }
    if (workspaceSize > kSystemWorkspaceBytes) {
        void* userWorkspaceAddr = static_cast<uint8_t*>(workspaceAddr) + kSystemWorkspaceBytes;
        const size_t userWorkspaceSize = workspaceSize - kSystemWorkspaceBytes;
        ret = aclrtMemset(userWorkspaceAddr, userWorkspaceSize, 0, userWorkspaceSize);
        CHECK_RET(ret == ACL_SUCCESS, LOG_PRINT("initialize user workspace failed. ERROR: %d\n", ret); return ret);
    }

    LOG_PRINT("calling aclnnNtt...\n");
    ret = aclnnNtt(workspaceAddr, workspaceSize, executor, stream);
    CHECK_RET(ret == ACL_SUCCESS, LOG_PRINT("aclnnNtt failed. ERROR: %d\n", ret); return ret);

    LOG_PRINT("synchronizing stream...\n");
    ret = aclrtSynchronizeStream(stream);
    CHECK_RET(ret == ACL_SUCCESS, LOG_PRINT("aclrtSynchronizeStream failed. ERROR: %d\n", ret); return ret);

    ret = aclrtMemcpy(
        output.data(), output.size() * sizeof(output[0]), outDeviceAddr, output.size() * sizeof(output[0]),
        ACL_MEMCPY_DEVICE_TO_HOST);
    CHECK_RET(ret == ACL_SUCCESS, LOG_PRINT("copy result failed. ERROR: %d\n", ret); return ret);
    if (IsMlKemProfile()) {
        output = RestoreKemParityPolynomials(output);
        CHECK_RET(!output.empty(), return -1);
    }
    CHECK_RET(CheckResult(output, golden), return -1);
    std::ofstream resultFile(OutputPath("output_z.bin"), std::ios::binary);
    resultFile.write(reinterpret_cast<const char*>(output.data()),
        static_cast<std::streamsize>(output.size() * sizeof(output[0])));
    CHECK_RET(resultFile.good(), LOG_PRINT("write NTT result failed\n"); return -1);
    resultFile.close();

    aclDestroyTensor(x);
    aclDestroyTensor(matrixPhase1Tensor);
    aclDestroyTensor(matrixPhase2Tensor);
    aclDestroyTensor(twiddleTensor);
    aclDestroyTensor(out);
    aclDestroyIntArray(qList);
    aclDestroyIntArray(gList);
    aclrtFree(xDeviceAddr);
    aclrtFree(matrixPhase1DeviceAddr);
    aclrtFree(matrixPhase2DeviceAddr);
    aclrtFree(twiddleDeviceAddr);
    aclrtFree(outDeviceAddr);
    if (workspaceSize > 0) {
        aclrtFree(workspaceAddr);
    }
    aclrtDestroyStream(stream);
    aclrtResetDevice(deviceId);
    aclFinalize();
    return 0;
}
