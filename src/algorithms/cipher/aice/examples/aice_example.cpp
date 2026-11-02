/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "cann_crypto/aice.h"
#include "acl/acl.h"
#include <array>
#include <cstdlib>
#include <iostream>

namespace {
bool InitializeDevice(int device)
{
    if (aclInit(nullptr) != ACL_SUCCESS) {
        return false;
    }
    if (aclrtSetDevice(device) != ACL_SUCCESS) {
        (void)aclFinalize();
        return false;
    }

    return true;
}

aclError AllocateBuffers(void*& deviceInput, void*& deviceOutput, void*& deviceKey, void*& deviceIv, size_t length)
{
    aclError status = aclrtMalloc(&deviceInput, length, ACL_MEM_MALLOC_HUGE_FIRST);
    if (status == ACL_SUCCESS) {
        status = aclrtMalloc(&deviceOutput, length, ACL_MEM_MALLOC_HUGE_FIRST);
    }
    if (status == ACL_SUCCESS) {
        status = aclrtMalloc(&deviceKey, 32, ACL_MEM_MALLOC_HUGE_FIRST);
    }
    if (status == ACL_SUCCESS) {
        status = aclrtMalloc(&deviceIv, 12, ACL_MEM_MALLOC_HUGE_FIRST);
    }
    return status;
}

void FreeBuffers(void* deviceInput, void* deviceOutput, void* deviceKey, void* deviceIv, aclrtStream stream)
{
    for (void* buffer : {deviceInput, deviceOutput, deviceKey, deviceIv}) {
        if (buffer != nullptr) {
            (void)aclrtFree(buffer);
        }
    }
    if (stream != nullptr) {
        (void)aclrtDestroyStream(stream);
    }
}
} // namespace

int main(int argc, char** argv)
{
    const int device = argc > 1 ? std::atoi(argv[1]) : 0;
    constexpr uint32_t batch = 128, dim = 1;
    // Fixed key and IV for this example.
    const std::array<uint8_t, 32> key{};
    const std::array<uint8_t, 12> iv{};
    std::array<uint8_t, 1024> plain{}, recovered{};
    for (size_t i = 0; i < plain.size(); ++i) {
        plain[i] = static_cast<uint8_t>(i);
    }

    if (!InitializeDevice(device))
        return 1;

    aclrtStream stream = nullptr;
    void* deviceInput = nullptr;
    void* deviceOutput = nullptr;
    void* deviceKey = nullptr;
    void* deviceIv = nullptr;
    aclError status = aclrtCreateStream(&stream);
    if (status == ACL_SUCCESS)
        status = AllocateBuffers(deviceInput, deviceOutput, deviceKey, deviceIv, plain.size());
    if (status == ACL_SUCCESS) {
        status = aclrtMemcpy(deviceInput, plain.size(), plain.data(), plain.size(), ACL_MEMCPY_HOST_TO_DEVICE);
    }
    if (status == ACL_SUCCESS) {
        status = aclrtMemcpy(deviceKey, key.size(), key.data(), key.size(), ACL_MEMCPY_HOST_TO_DEVICE);
    }
    if (status == ACL_SUCCESS) {
        status = aclrtMemcpy(deviceIv, iv.size(), iv.data(), iv.size(), ACL_MEMCPY_HOST_TO_DEVICE);
    }

    // Both public calls complete synchronously on the supplied stream.
    if (status == ACL_SUCCESS) {
        status = CannCryptoAiceEncrypt(
            static_cast<uint8_t*>(deviceInput), plain.size(), static_cast<uint8_t*>(deviceOutput), plain.size(),
            static_cast<uint8_t*>(deviceKey), static_cast<uint8_t*>(deviceIv), batch, dim, stream);
    }
    if (status == ACL_SUCCESS) {
        status = CannCryptoAiceDecrypt(
            static_cast<uint8_t*>(deviceOutput), plain.size(), static_cast<uint8_t*>(deviceInput), plain.size(),
            static_cast<uint8_t*>(deviceKey), static_cast<uint8_t*>(deviceIv), batch, dim, stream);
    }
    if (status == ACL_SUCCESS) {
        status =
            aclrtMemcpy(recovered.data(), recovered.size(), deviceInput, recovered.size(), ACL_MEMCPY_DEVICE_TO_HOST);
    }

    FreeBuffers(deviceInput, deviceOutput, deviceKey, deviceIv, stream);
    (void)aclrtResetDevice(device);
    (void)aclFinalize();

    const bool passed = status == ACL_SUCCESS && recovered == plain;
    std::cout << (passed ? "AICE_EXAMPLE=PASS" : "AICE_EXAMPLE=FAIL") << '\n';
    return passed ? 0 : 1;
}
