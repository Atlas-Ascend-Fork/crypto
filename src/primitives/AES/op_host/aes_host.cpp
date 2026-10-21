/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "cann_crypto/aes.h"
#include "aes_launch.h"
#include "../op_kernel/aes_tiling.h"
#include "acl/acl.h"

#include <algorithm>
#include <limits>

namespace {
constexpr uint64_t ALIGN = 512;
constexpr uint64_t MAX_INPUT_LENGTH = static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) - (ALIGN - 1);

struct DeviceBuffer {
    void* data = nullptr;
    ~DeviceBuffer()
    {
        if (data != nullptr) {
            (void)aclrtFree(data);
        }
    }
    aclError Allocate(size_t size) { return aclrtMalloc(&data, size, ACL_MEM_MALLOC_HUGE_FIRST); }
};

bool ValidBuffers(
    const uint8_t* input, uint64_t size, uint8_t* output, uint64_t capacity, const uint8_t* key, const uint8_t* nonce,
    uint64_t tagSize)
{
    if (size > MAX_INPUT_LENGTH || output == nullptr || key == nullptr || nonce == nullptr ||
        (size != 0 && input == nullptr)) {
        return false;
    }
    const uint64_t aligned = (size + ALIGN - 1) / ALIGN * ALIGN;
    return capacity >= aligned + tagSize;
}

aclError PrepareInput(
    const uint8_t* input, uint32_t size, uint32_t aligned, DeviceBuffer& padding, void*& deviceInput, void* stream)
{
    deviceInput = const_cast<uint8_t*>(input);
    if (size == aligned) {
        return ACL_SUCCESS;
    }
    // aclrtMemcpy does not wait for producers queued on the caller's stream.
    aclError status = aclrtSynchronizeStream(stream);
    if (status != ACL_SUCCESS) {
        return status;
    }
    status = padding.Allocate(aligned);
    if (status != ACL_SUCCESS) {
        return status;
    }
    status = aclrtMemset(padding.data, aligned, 0, aligned);
    if (status != ACL_SUCCESS) {
        return status;
    }
    status = aclrtMemcpy(padding.data, aligned, input, size, ACL_MEMCPY_DEVICE_TO_DEVICE);
    deviceInput = padding.data;
    return status;
}

template <typename T>
aclError CopyTiling(const T& tiling, DeviceBuffer& device)
{
    const aclError status = device.Allocate(sizeof(T));
    if (status != ACL_SUCCESS) {
        return status;
    }
    return aclrtMemcpy(device.data, sizeof(T), &tiling, sizeof(T), ACL_MEMCPY_HOST_TO_DEVICE);
}
Aes128GcmTilingData MakeGcmTiling(
    uint64_t inputBytes, const uint8_t* roundKeys, const uint8_t* nonce, const uint8_t* aad, uint32_t aadBytes)
{
    Aes128GcmTilingData tiling{};
    std::copy_n(roundKeys, sizeof(tiling.rd_key), reinterpret_cast<uint8_t*>(tiling.rd_key));
    std::copy_n(nonce, sizeof(tiling.nonce), tiling.nonce);
    if (aadBytes != 0) {
        std::copy_n(aad, aadBytes, tiling.aad);
    }
    tiling.aad_len = aadBytes;
    tiling.totalDataSize = static_cast<uint32_t>((inputBytes + ALIGN - 1) / ALIGN * ALIGN);
    tiling.numCores = 40;
    tiling.ghashCoreNum = 20;
    return tiling;
}
} // namespace

extern "C" int32_t CannCryptoAes128Ctr(
    const uint8_t* input, uint64_t inputBytes, uint8_t* output, uint64_t outputCapacity, const uint8_t* roundKeys,
    const uint8_t* nonce, void* stream)
{
    if (inputBytes == 0 || !ValidBuffers(input, inputBytes, output, outputCapacity, roundKeys, nonce, 0)) {
        return ACL_ERROR_INVALID_PARAM;
    }
    const uint32_t aligned = static_cast<uint32_t>((inputBytes + ALIGN - 1) / ALIGN * ALIGN);
    ascend_ops::AesCtr::Aes128CtrTilingData tiling{};
    std::copy_n(roundKeys, sizeof(tiling.rd_key), reinterpret_cast<uint8_t*>(tiling.rd_key));
    std::copy_n(nonce, sizeof(tiling.nonce), tiling.nonce);
    tiling.input_length = static_cast<uint32_t>(inputBytes);
    DeviceBuffer padding, tilingDevice;
    void* deviceInput = nullptr;
    aclError status = PrepareInput(input, tiling.input_length, aligned, padding, deviceInput, stream);
    if (status != ACL_SUCCESS) {
        return status;
    }
    status = CopyTiling(tiling, tilingDevice);
    if (status != ACL_SUCCESS) {
        return status;
    }
    CannCryptoLaunchAesCtr(deviceInput, output, tilingDevice.data, stream);
    return aclrtSynchronizeStream(stream);
}

extern "C" int32_t CannCryptoAes128Gcm(
    const uint8_t* input, uint64_t inputBytes, uint8_t* output, uint64_t outputCapacity, const uint8_t* roundKeys,
    const uint8_t* nonce, const uint8_t* aad, uint32_t aadBytes, void* stream)
{
    if (!ValidBuffers(input, inputBytes, output, outputCapacity, roundKeys, nonce, 16) || aadBytes > 32 ||
        (aadBytes != 0 && aad == nullptr)) {
        return ACL_ERROR_INVALID_PARAM;
    }
    const Aes128GcmTilingData tiling = MakeGcmTiling(inputBytes, roundKeys, nonce, aad, aadBytes);
    DeviceBuffer padding, tilingDevice, precomputed, intermediate;
    void* deviceInput = nullptr;
    aclError status =
        PrepareInput(input, static_cast<uint32_t>(inputBytes), tiling.totalDataSize, padding, deviceInput, stream);
    if (status != ACL_SUCCESS) {
        return status;
    }
    status = CopyTiling(tiling, tilingDevice);
    if (status != ACL_SUCCESS) {
        return status;
    }
    status = precomputed.Allocate(64);
    if (status != ACL_SUCCESS) {
        return status;
    }
    status = intermediate.Allocate(20 * 32);
    if (status != ACL_SUCCESS) {
        return status;
    }
    CannCryptoLaunchAesGcmPrecompute(precomputed.data, tilingDevice.data, stream);
    status = aclrtSynchronizeStream(stream);
    if (status != ACL_SUCCESS) {
        return status;
    }
    CannCryptoLaunchAesGcmEncrypt(tiling.totalDataSize, deviceInput, output, tilingDevice.data, stream);
    status = aclrtSynchronizeStream(stream);
    if (status != ACL_SUCCESS) {
        return status;
    }
    CannCryptoLaunchAesGcmGhash(output, intermediate.data, precomputed.data, tilingDevice.data, stream);
    status = aclrtSynchronizeStream(stream);
    if (status != ACL_SUCCESS) {
        return status;
    }
    CannCryptoLaunchAesGcmTag(output, intermediate.data, precomputed.data, tilingDevice.data, stream);
    return aclrtSynchronizeStream(stream);
}
