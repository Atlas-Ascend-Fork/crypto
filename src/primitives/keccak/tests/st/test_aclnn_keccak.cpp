#include <acl/acl.h>
#include <aclnn_keccak_f1600_tensor.h>

#include "sha3_ref.h"
#include "../../examples/acl_example.h"

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

} // namespace

int main(int argc, char** argv)
{
    aclrtStream stream = nullptr;
    bool passed = true;
    const int deviceId = argc > 1 ? std::stoi(argv[1]) : 0;
    try {
        RequireAcl(aclInit(nullptr), "aclInit");
        RequireAcl(aclrtSetDevice(deviceId), "aclrtSetDevice");
        example::CheckDeviceTarget();
        RequireAcl(aclrtCreateStream(&stream), "aclrtCreateStream");
        for (const size_t batch : {64U, 256U, 1024U, 4096U, 8192U}) {
            passed = RunKeccakCase(batch, stream) && passed;
        }
        RequireAcl(aclrtDestroyStream(stream), "aclrtDestroyStream");
        stream = nullptr;
        RequireAcl(aclrtResetDevice(deviceId), "aclrtResetDevice");
        RequireAcl(aclFinalize(), "aclFinalize");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        if (stream != nullptr) { aclrtDestroyStream(stream); }
        aclrtResetDevice(deviceId);
        aclFinalize();
        passed = false;
    }
    std::cout << "CORRECTNESS_RESULT=" << (passed ? "PASS" : "FAIL") << '\n';
    return passed ? 0 : 1;
}
