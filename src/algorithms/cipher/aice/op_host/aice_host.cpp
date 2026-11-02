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
#include "aice_launch.h"
#include "acl/acl.h"
#include <limits>

namespace {
bool ValidRange(const void* pointer, uint64_t size)
{
    return pointer != nullptr && size <= std::numeric_limits<uintptr_t>::max() - reinterpret_cast<uintptr_t>(pointer);
}
// Both ranges must pass ValidRange first, so their exclusive ends cannot wrap.
bool Overlaps(const void* first, uint64_t firstLength, const void* second, uint64_t secondLength)
{
    const auto firstBegin = reinterpret_cast<uintptr_t>(first);
    const auto secondBegin = reinterpret_cast<uintptr_t>(second);
    return firstBegin < secondBegin + secondLength && secondBegin < firstBegin + firstLength;
}
int32_t Crypt(
    bool encrypt, const uint8_t* input, uint64_t length, uint8_t* output, uint64_t capacity, const uint8_t* key,
    const uint8_t* iv, uint32_t batch, uint32_t dim, void* stream)
{
    if ((batch != 128 && batch != 256 && batch != 512) || dim == 0 || uint64_t(batch) * dim > UINT32_MAX ||
        length > UINT32_MAX || capacity < length) {
        return ACL_ERROR_INVALID_PARAM;
    }
    if (length == 0) {
        return aclrtSynchronizeStream(stream);
    }
    if (!ValidRange(input, length) || !ValidRange(output, length) || !ValidRange(key, 32) || !ValidRange(iv, 12)) {
        return ACL_ERROR_INVALID_PARAM;
    }
    if ((input != output && Overlaps(input, length, output, length)) || Overlaps(output, length, key, 32) ||
        Overlaps(output, length, iv, 12)) {
        return ACL_ERROR_INVALID_PARAM;
    }
    const aclError status =
        CannCryptoAiceLaunch(batch, encrypt, dim, stream, input, key, iv, output, static_cast<uint32_t>(length));
    // Drain any submitted work even if the launcher reports an error.
    const aclError syncStatus = aclrtSynchronizeStream(stream);
    return status != ACL_SUCCESS ? status : syncStatus;
}
} // namespace

extern "C" int32_t CannCryptoAiceEncrypt(
    const uint8_t* input, uint64_t length, uint8_t* output, uint64_t capacity, const uint8_t* key, const uint8_t* iv,
    uint32_t batch, uint32_t dim, void* stream)
{
    return Crypt(true, input, length, output, capacity, key, iv, batch, dim, stream);
}
extern "C" int32_t CannCryptoAiceDecrypt(
    const uint8_t* input, uint64_t length, uint8_t* output, uint64_t capacity, const uint8_t* key, const uint8_t* iv,
    uint32_t batch, uint32_t dim, void* stream)
{
    return Crypt(false, input, length, output, capacity, key, iv, batch, dim, stream);
}
