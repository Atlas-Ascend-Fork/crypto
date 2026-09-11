#include <acl/acl.h>
#include <aclnn_keccak_f1600_tensor.h>
#include <aclnn_shake128_tensor.h>
#include <aclnn_shake256_tensor.h>
#include <openssl/evp.h>

#include "sha3_ref.h"
#include "../../../../../primitives/keccak/examples/acl_example.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using GetWorkspaceFn = aclnnStatus (*)(
    const aclTensor*, const aclTensor*, uint64_t*, aclOpExecutor**);
using LaunchFn = aclnnStatus (*)(
    void*, uint64_t, aclOpExecutor*, aclrtStream);
using EvpFn = const EVP_MD* (*)();

struct Algorithm {
    const char* name;
    size_t rate;
    size_t fastOutputLength;
    GetWorkspaceFn getWorkspace;
    LaunchFn launch;
    EvpFn evp;
};

struct TestCase {
    size_t batch;
    size_t inputLength;
    size_t outputLength;
    uint32_t seed;
};

const Algorithm SHAKE128 = {
    "SHAKE128",
    168,
    32,
    aclnnShake128TensorGetWorkspaceSize,
    aclnnShake128Tensor,
    EVP_shake128};

const Algorithm SHAKE256 = {
    "SHAKE256",
    136,
    64,
    aclnnShake256TensorGetWorkspaceSize,
    aclnnShake256Tensor,
    EVP_shake256};

void RequireAcl(aclError status, const char* expression)
{
    if (status != ACL_SUCCESS) {
        const char* detail = aclGetRecentErrMsg();
        throw std::runtime_error(
            std::string("ACL 调用失败：") + expression +
            "，返回值=" + std::to_string(status) +
            (detail == nullptr ? "" : std::string("\n") + detail));
    }
}

void RequireAclnn(aclnnStatus status, const char* expression)
{
    if (status != 0) {
        const char* detail = aclGetRecentErrMsg();
        throw std::runtime_error(
            std::string("ACLNN 调用失败：") + expression +
            "，返回值=" + std::to_string(status) +
            (detail == nullptr ? "" : std::string("\n") + detail));
    }
}

class DeviceBuffer {
public:
    explicit DeviceBuffer(size_t logicalBytes) : logicalBytes_(logicalBytes)
    {
        const size_t allocationBytes = logicalBytes_ == 0 ? 1 : logicalBytes_;
        RequireAcl(
            aclrtMalloc(&data_, allocationBytes, ACL_MEM_MALLOC_HUGE_FIRST),
            "aclrtMalloc");
    }

    ~DeviceBuffer()
    {
        if (data_ != nullptr) {
            aclrtFree(data_);
        }
    }

    DeviceBuffer(const DeviceBuffer&) = delete;
    DeviceBuffer& operator=(const DeviceBuffer&) = delete;
    void* get() const { return data_; }
    size_t size() const { return logicalBytes_; }

private:
    void* data_ = nullptr;
    size_t logicalBytes_ = 0;
};

class TensorHandle {
public:
    TensorHandle(
        void* deviceData,
        const std::vector<int64_t>& shape,
        aclDataType dataType = ACL_UINT8)
    {
        std::vector<int64_t> strides(shape.size());
        int64_t stride = 1;
        for (int index = static_cast<int>(shape.size()) - 1;
             index >= 0;
             --index) {
            strides[static_cast<size_t>(index)] = stride;
            stride *= shape[static_cast<size_t>(index)];
        }

        tensor_ = aclCreateTensor(
            shape.data(), shape.size(), dataType,
            strides.data(), 0, ACL_FORMAT_ND,
            shape.data(), shape.size(), deviceData);
        if (tensor_ == nullptr) {
            throw std::runtime_error("aclCreateTensor returned nullptr");
        }
    }

    ~TensorHandle()
    {
        if (tensor_ != nullptr) {
            aclDestroyTensor(tensor_);
        }
    }

    TensorHandle(const TensorHandle&) = delete;
    TensorHandle& operator=(const TensorHandle&) = delete;
    aclTensor* get() const { return tensor_; }

private:
    aclTensor* tensor_ = nullptr;
};

void OpenSslShake(
    const Algorithm& algorithm,
    const uint8_t* input,
    size_t inputLength,
    uint8_t* output,
    size_t outputLength)
{
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    if (context == nullptr) {
        throw std::runtime_error("EVP_MD_CTX_new 创建失败");
    }

    const uint8_t empty = 0;
    const uint8_t* inputPointer = inputLength == 0 ? &empty : input;
    const int initStatus = EVP_DigestInit_ex(context, algorithm.evp(), nullptr);
    const int updateStatus = EVP_DigestUpdate(context, inputPointer, inputLength);
    const int finalStatus = EVP_DigestFinalXOF(context, output, outputLength);
    EVP_MD_CTX_free(context);
    if (initStatus != 1 || updateStatus != 1 || finalStatus != 1) {
        throw std::runtime_error("OpenSSL SHAKE 参考计算失败");
    }
}

uint64_t Checksum(const std::vector<uint8_t>& bytes)
{
    uint64_t hash = 1469598103934665603ULL;
    for (uint8_t byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

void PackKeccakStates(
    const std::vector<uint8_t>& states,
    size_t batch,
    std::vector<uint32_t>& packed)
{
    packed.assign(50 * batch, 0U);
    for (size_t message = 0; message < batch; ++message) {
        const uint8_t* state = states.data() + message * 200;
        for (size_t lane = 0; lane < 25; ++lane) {
            const uint64_t value = CannCryptoRefLoad64Le(state + lane * 8);
            packed[(2 * lane) * batch + message] =
                static_cast<uint32_t>(value & 0xffffffffULL);
            packed[(2 * lane + 1) * batch + message] =
                static_cast<uint32_t>(value >> 32);
        }
    }
}

void UnpackKeccakStates(
    const std::vector<uint32_t>& packed,
    size_t batch,
    std::vector<uint8_t>& states)
{
    states.assign(batch * 200, 0U);
    for (size_t message = 0; message < batch; ++message) {
        uint8_t* state = states.data() + message * 200;
        for (size_t lane = 0; lane < 25; ++lane) {
            const uint64_t value =
                static_cast<uint64_t>(packed[(2 * lane) * batch + message]) |
                (static_cast<uint64_t>(
                    packed[(2 * lane + 1) * batch + message]) << 32);
            CannCryptoRefStore64Le(state + lane * 8, value);
        }
    }
}

bool RunKeccakCase(size_t batch, aclrtStream stream)
{
    std::vector<uint8_t> inputStates(batch * 200);
    std::mt19937 generator(static_cast<uint32_t>(20260517U + batch));
    for (uint8_t& byte : inputStates) {
        byte = static_cast<uint8_t>(generator() & 0xffU);
    }

    std::vector<uint8_t> expected = inputStates;
    for (size_t message = 0; message < batch; ++message) {
        uint64_t state[25];
        for (size_t lane = 0; lane < 25; ++lane) {
            state[lane] = CannCryptoRefLoad64Le(
                expected.data() + message * 200 + lane * 8);
        }
        CannCryptoRefKeccakF1600(state);
        for (size_t lane = 0; lane < 25; ++lane) {
            CannCryptoRefStore64Le(
                expected.data() + message * 200 + lane * 8,
                state[lane]);
        }
    }

    std::vector<uint32_t> inputPacked;
    PackKeccakStates(inputStates, batch, inputPacked);
    std::vector<uint32_t> outputPacked(50 * batch, 0U);

    DeviceBuffer deviceInput(inputPacked.size() * sizeof(uint32_t));
    DeviceBuffer deviceOutput(outputPacked.size() * sizeof(uint32_t));
    RequireAcl(
        aclrtMemcpy(
            deviceInput.get(), deviceInput.size(),
            inputPacked.data(), inputPacked.size() * sizeof(uint32_t),
            ACL_MEMCPY_HOST_TO_DEVICE),
        "Keccak 输入由主机复制到设备");
    RequireAcl(
        aclrtMemset(
            deviceOutput.get(), deviceOutput.size(), 0, deviceOutput.size()),
        "Keccak 输出清零");

    TensorHandle inputTensor(
        deviceInput.get(),
        {50, static_cast<int64_t>(batch)},
        ACL_UINT32);
    TensorHandle outputTensor(
        deviceOutput.get(),
        {50, static_cast<int64_t>(batch)},
        ACL_UINT32);

    uint64_t workspaceSize = 0;
    aclOpExecutor* executor = nullptr;
    RequireAclnn(
        aclnnKeccakF1600TensorGetWorkspaceSize(
            inputTensor.get(), outputTensor.get(),
            &workspaceSize, &executor),
        "Keccak GetWorkspaceSize");
    DeviceBuffer workspace(workspaceSize);
    RequireAclnn(
        aclnnKeccakF1600Tensor(
            workspaceSize == 0 ? nullptr : workspace.get(),
            workspaceSize, executor, stream),
        "Keccak 算子执行");
    RequireAcl(aclrtSynchronizeStream(stream), "Keccak 流同步");
    RequireAcl(
        aclrtMemcpy(
            outputPacked.data(), outputPacked.size() * sizeof(uint32_t),
            deviceOutput.get(), deviceOutput.size(),
            ACL_MEMCPY_DEVICE_TO_HOST),
        "Keccak 输出由设备复制到主机");

    std::vector<uint8_t> actual;
    UnpackKeccakStates(outputPacked, batch, actual);
    const auto mismatch = std::mismatch(
        actual.begin(), actual.end(), expected.begin());
    if (mismatch.first != actual.end()) {
        const size_t offset = static_cast<size_t>(
            mismatch.first - actual.begin());
        std::cerr
            << "[失败] KeccakF1600Tensor"
            << " batch=" << batch
            << " 消息序号=" << (offset / 200)
            << " 字节序号=" << (offset % 200)
            << " 实际值=" << static_cast<unsigned>(*mismatch.first)
            << " 期望值=" << static_cast<unsigned>(*mismatch.second)
            << std::endl;
        return false;
    }

    std::cout
        << "[通过] KeccakF1600Tensor"
        << " batch=" << batch
        << " 校验和=" << Checksum(actual)
        << std::endl;
    return true;
}

std::vector<uint8_t> MakeInput(const TestCase& testCase)
{
    std::vector<uint8_t> input(testCase.batch * testCase.inputLength);
    std::mt19937 generator(testCase.seed);
    for (uint8_t& byte : input) {
        byte = static_cast<uint8_t>(generator() & 0xffU);
    }

    if (testCase.batch > 0 && testCase.inputLength > 0) {
        std::fill(
            input.begin(),
            input.begin() + static_cast<std::ptrdiff_t>(testCase.inputLength),
            0U);
    }
    if (testCase.batch > 1 && testCase.inputLength > 0) {
        for (size_t byte = 0; byte < testCase.inputLength; ++byte) {
            input[testCase.inputLength + byte] =
                static_cast<uint8_t>(byte & 0xffU);
        }
    }
    return input;
}

bool RunCase(
    const Algorithm& algorithm,
    const TestCase& testCase,
    aclrtStream stream)
{
    std::vector<uint8_t> input = MakeInput(testCase);
    std::vector<uint8_t> expected(
        testCase.batch * testCase.outputLength, 0U);
    std::vector<uint8_t> actual(expected.size(), 0U);

    for (size_t message = 0; message < testCase.batch; ++message) {
        const uint8_t* inputRow = testCase.inputLength == 0
            ? nullptr
            : input.data() + message * testCase.inputLength;
        OpenSslShake(
            algorithm,
            inputRow,
            testCase.inputLength,
            expected.data() + message * testCase.outputLength,
            testCase.outputLength);
    }

    DeviceBuffer deviceInput(input.size());
    DeviceBuffer deviceOutput(actual.size());
    if (!input.empty()) {
        RequireAcl(
            aclrtMemcpy(
                deviceInput.get(), deviceInput.size(),
                input.data(), input.size(), ACL_MEMCPY_HOST_TO_DEVICE),
            "input H2D");
    }
    RequireAcl(
        aclrtMemset(
            deviceOutput.get(), deviceOutput.size(), 0, deviceOutput.size()),
        "output memset");

    TensorHandle inputTensor(
        deviceInput.get(),
        {static_cast<int64_t>(testCase.batch),
         static_cast<int64_t>(testCase.inputLength)});
    TensorHandle outputTensor(
        deviceOutput.get(),
        {static_cast<int64_t>(testCase.batch),
         static_cast<int64_t>(testCase.outputLength)});

    uint64_t workspaceSize = 0;
    aclOpExecutor* executor = nullptr;
    RequireAclnn(
        algorithm.getWorkspace(
            inputTensor.get(), outputTensor.get(),
            &workspaceSize, &executor),
        "GetWorkspaceSize");
    DeviceBuffer workspace(workspaceSize);
    RequireAclnn(
        algorithm.launch(
            workspaceSize == 0 ? nullptr : workspace.get(),
            workspaceSize, executor, stream),
        "算子执行");
    RequireAcl(aclrtSynchronizeStream(stream), "aclrtSynchronizeStream");
    RequireAcl(
        aclrtMemcpy(
            actual.data(), actual.size(),
            deviceOutput.get(), deviceOutput.size(),
            ACL_MEMCPY_DEVICE_TO_HOST),
        "输出由设备复制到主机");

    const auto mismatch = std::mismatch(
        actual.begin(), actual.end(), expected.begin());
    if (mismatch.first != actual.end()) {
        const size_t offset = static_cast<size_t>(
            mismatch.first - actual.begin());
        std::cerr
            << "[失败] " << algorithm.name
            << " batch=" << testCase.batch
            << " 输入长度=" << testCase.inputLength
            << " 输出长度=" << testCase.outputLength
            << " 消息序号=" << (offset / testCase.outputLength)
            << " 字节序号=" << (offset % testCase.outputLength)
            << " 实际值=" << static_cast<unsigned>(*mismatch.first)
            << " 期望值=" << static_cast<unsigned>(*mismatch.second)
            << std::endl;
        return false;
    }

    std::cout
        << "[通过] " << algorithm.name
        << " batch=" << testCase.batch
        << " 输入长度=" << testCase.inputLength
        << " 输出长度=" << testCase.outputLength
        << " 校验和=" << Checksum(actual)
        << std::endl;
    return true;
}

std::vector<TestCase> BuildCases(const Algorithm& algorithm)
{
    const size_t rate = algorithm.rate;
    return {
        {1, 0, 1, 1001U},
        {2, 1, 3, 1002U},
        {3, 3, 31, 1003U},
        {7, 4, 32, 1004U},
        {15, 63, algorithm.fastOutputLength, 1005U},
        {16, 64, algorithm.fastOutputLength, 1006U},
        {17, 65, 33, 1007U},
        {31, rate - 1, rate - 1, 1008U},
        {32, rate, rate, 1009U},
        {33, rate + 1, rate + 1, 1010U},
        {63, 64, algorithm.fastOutputLength, 1011U},
        {64, rate - 1, 1, 1012U},
        {65, 2 * rate - 1, 64, 1013U},
        {207, rate, 32, 1014U},
        {208, rate + 1, 64, 1015U},
        {209, 2 * rate, rate + 1, 1016U},
        {257, 2 * rate + 1, 2 * rate + 3, 1017U},
        {1024, 64, algorithm.fastOutputLength, 1018U},
    };
}

std::vector<TestCase> BuildStressCases(const Algorithm& algorithm)
{
    return {
        {1024, algorithm.rate + 1, algorithm.rate + 1, 20260803U},
        {8192, 64, algorithm.fastOutputLength, 20260804U},
    };
}

}  // namespace

int main(int argc, char** argv)
{
    const int deviceId = argc > 1 ? std::atoi(argv[1]) : 0;
    const bool stressOnly = argc > 2 &&
        std::string(argv[2]) == "--stress-case";

    bool passed = true;
    aclrtStream stream = nullptr;
    try {
        RequireAcl(aclInit(nullptr), "aclInit");
        RequireAcl(aclrtSetDevice(deviceId), "aclrtSetDevice");
        example::CheckDeviceTarget();
        RequireAcl(aclrtCreateStream(&stream), "aclrtCreateStream");

        if (!stressOnly) {
            for (size_t batch : {64U, 256U, 1024U, 4096U, 8192U}) {
                passed = RunKeccakCase(batch, stream) && passed;
            }
        }

        for (const Algorithm& algorithm : {SHAKE128, SHAKE256}) {
            const std::vector<TestCase> cases = stressOnly
                ? BuildStressCases(algorithm)
                : BuildCases(algorithm);
            for (const TestCase& testCase : cases) {
                passed = RunCase(algorithm, testCase, stream) && passed;
            }
        }

        RequireAcl(aclrtDestroyStream(stream), "aclrtDestroyStream");
        stream = nullptr;
        RequireAcl(aclrtResetDevice(deviceId), "aclrtResetDevice");
        RequireAcl(aclFinalize(), "aclFinalize");
    } catch (const std::exception& error) {
        std::cerr << "[失败] " << error.what() << std::endl;
        passed = false;
        if (stream != nullptr) {
            aclrtDestroyStream(stream);
        }
        aclrtResetDevice(deviceId);
        aclFinalize();
    }

    std::cout << "CORRECTNESS_RESULT="
              << (passed ? "PASS" : "FAIL")
              << std::endl;
    return passed ? 0 : 1;
}
