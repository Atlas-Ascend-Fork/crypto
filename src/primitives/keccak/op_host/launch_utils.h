/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_CRYPTO_LAUNCH_UTILS_H
#define CANN_CRYPTO_LAUNCH_UTILS_H

#include <acl/acl.h>
#include <cstdint>
#include <limits>

namespace cann_crypto::detail {
inline aclError GetVectorCoreCount(uint32_t& coreCount)
{
    int32_t device = 0;
    aclError status = aclrtGetDevice(&device);
    if (status != ACL_SUCCESS) {
        return status;
    }
    int64_t count = 0;
    status = aclrtGetDeviceInfo(device, ACL_DEV_ATTR_VECTOR_CORE_NUM, &count);
    if (status != ACL_SUCCESS) {
        return status;
    }
    if (count <= 0 || static_cast<uint64_t>(count) > std::numeric_limits<uint32_t>::max()) {
        return ACL_ERROR_INVALID_PARAM;
    }
    coreCount = static_cast<uint32_t>(count);
    return ACL_SUCCESS;
}

inline bool ValidBuffers(const void* input, uint64_t inputBytes, const void* output, uint64_t outputBytes)
{
    if (output == nullptr || (inputBytes != 0U && input == nullptr)) {
        return false;
    }
    const auto src = reinterpret_cast<uintptr_t>(input);
    const auto dst = reinterpret_cast<uintptr_t>(output);
    const auto limit = std::numeric_limits<uintptr_t>::max();
    if (outputBytes > limit - dst || (inputBytes != 0U && inputBytes > limit - src)) {
        return false;
    }
    return inputBytes == 0U || src + inputBytes <= dst || dst + outputBytes <= src;
}
} // namespace cann_crypto::detail
#endif
