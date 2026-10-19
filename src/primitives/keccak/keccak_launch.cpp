/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "cann_crypto/keccak.h"
#include "keccak_internal.h"
#include "launch_utils.h"

extern "C" void CannCryptoKeccakLaunch(
    uint32_t blockDim, void* stream, const uint32_t* input, uint32_t* output, uint32_t batch);

extern "C" int32_t CannCryptoKeccakF1600(const uint32_t* input, uint32_t* output, uint64_t batch, void* stream)
{
    if (batch == 0U || batch > 8192U || !cann_crypto::detail::ValidBuffers(input, batch * 200U, output, batch * 200U)) {
        return ACL_ERROR_INVALID_PARAM;
    }
    uint32_t coreCount = 0;
    const aclError status = cann_crypto::detail::GetVectorCoreCount(coreCount);
    if (status != ACL_SUCCESS) {
        return status;
    }
    cann_crypto::keccak::LaunchConfig config{};
    if (!cann_crypto::keccak::BuildLaunchConfig(batch, coreCount, config)) {
        return ACL_ERROR_INVALID_PARAM;
    }
    CannCryptoKeccakLaunch(config.blockDim, stream, input, output, config.batch);
    return aclrtSynchronizeStream(stream);
}
