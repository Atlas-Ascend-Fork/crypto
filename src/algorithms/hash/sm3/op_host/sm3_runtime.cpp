/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#include "cann_crypto/sm3.h"
#include "../sm3_internal.h"
#include "acl/acl.h"
#include "tiling/platform/platform_ascendc.h"
#include <algorithm>
extern "C" void CannCryptoSm3Launch(
    uint32_t blocks, void* stream, const uint32_t* rows, const uint8_t* bytes, uint8_t* result, uint32_t count,
    uint32_t uniformLength, uint32_t uniformMode);

int32_t CannCryptoSm3RunDirectNpu(const uint8_t* input, uint8_t* output, uint32_t count, uint32_t length, void* stream)
{
    auto* platform = platform_ascendc::PlatformAscendCManager::GetInstance();
    const uint32_t cores = platform ? platform->GetCoreNumAiv() : 0;
    if (!cores)
        return CANN_CRYPTO_SM3_RUNTIME_FAILURE;
    CannCryptoSm3Launch(std::min(cores, count), stream, nullptr, input, output, count, length, 1);
    return aclrtSynchronizeStream(stream) == ACL_SUCCESS ? CANN_CRYPTO_SM3_SUCCESS : CANN_CRYPTO_SM3_RUNTIME_FAILURE;
}
