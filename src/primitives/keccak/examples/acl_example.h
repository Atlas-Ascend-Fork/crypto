/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_CRYPTO_ACL_EXAMPLE_H
#define CANN_CRYPTO_ACL_EXAMPLE_H

#include <acl/acl.h>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

namespace example {
inline void Check(int status, const char* operation)
{
    if (status != 0) {
        const char* detail = aclGetRecentErrMsg();
        throw std::runtime_error(std::string(operation) + ": " + std::to_string(status) +
            (detail == nullptr ? "" : std::string("\n") + detail));
    }
}

inline void CheckDeviceTarget()
{
    const char* target = std::getenv("CANN_CRYPTO_SOC_TARGET");
    if (target == nullptr) {
        return;
    }
    const char* name = aclrtGetSocName();
    if (name == nullptr) {
        throw std::runtime_error("aclrtGetSocName returned nullptr");
    }
    const std::string soc(name);
    const std::string expected(target);
    const bool matches = (expected == "ascend910b" && soc.rfind("Ascend910B", 0) == 0) ||
        (expected == "ascend910_93" && soc.rfind("Ascend910_93", 0) == 0);
    if (!matches) {
        throw std::runtime_error("Operator package target " + expected + " does not match device " + soc);
    }
}

class Session {
public:
    explicit Session(int device) : device_(device)
    {
        Check(aclInit(nullptr), "aclInit");
        try {
            Check(aclrtSetDevice(device_), "aclrtSetDevice");
            active_ = true;
            CheckDeviceTarget();
            Check(aclrtCreateStream(&stream_), "aclrtCreateStream");
        } catch (...) {
            if (active_) { aclrtResetDevice(device_); }
            aclFinalize();
            throw;
        }
    }
    ~Session()
    {
        if (stream_ != nullptr) { aclrtDestroyStream(stream_); }
        if (active_) { aclrtResetDevice(device_); }
        aclFinalize();
    }
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    aclrtStream stream() const { return stream_; }
private:
    int device_;
    bool active_ = false;
    aclrtStream stream_ = nullptr;
};

class Buffer {
public:
    explicit Buffer(size_t bytes) : bytes_(bytes)
    {
        Check(aclrtMalloc(&data_, bytes == 0 ? 1 : bytes, ACL_MEM_MALLOC_HUGE_FIRST), "aclrtMalloc");
    }
    ~Buffer() { if (data_ != nullptr) { aclrtFree(data_); } }
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    void* data() const { return data_; }
    size_t size() const { return bytes_; }
private:
    void* data_ = nullptr;
    size_t bytes_;
};

class Tensor {
public:
    Tensor(const Buffer& buffer, const std::vector<int64_t>& shape, aclDataType type)
    {
        std::vector<int64_t> strides(shape.size(), 1);
        for (size_t index = shape.size(); index > 1; --index) {
            strides[index - 2] = strides[index - 1] * shape[index - 1];
        }
        tensor_ = aclCreateTensor(shape.data(), shape.size(), type, strides.data(), 0,
            ACL_FORMAT_ND, shape.data(), shape.size(), buffer.data());
        if (tensor_ == nullptr) { throw std::runtime_error("aclCreateTensor"); }
    }
    ~Tensor() { if (tensor_ != nullptr) { aclDestroyTensor(tensor_); } }
    Tensor(const Tensor&) = delete;
    Tensor& operator=(const Tensor&) = delete;
    aclTensor* get() const { return tensor_; }
private:
    aclTensor* tensor_ = nullptr;
};
}
#endif
