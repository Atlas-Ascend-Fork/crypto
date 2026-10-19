/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "cann_crypto/shake.h"
#include "shake_internal.h"
#include "../../../primitives/keccak/launch_utils.h"

extern "C" void CannCryptoShakeLaunch(
    uint32_t bits, uint32_t blockDim, void* stream, const uint8_t* input, uint8_t* output, uint32_t batch,
    uint32_t inputLength, uint32_t outputLength, uint32_t groupSize);

namespace {
int32_t RunShake(
    cann_crypto::shake::Algorithm algorithm, const uint8_t* input, uint8_t* output, uint64_t batch,
    uint64_t inputLength, uint64_t outputLength, void* stream)
{
    cann_crypto::shake::LaunchConfig config{};
    const cann_crypto::shake::Request request{algorithm, batch, inputLength, outputLength};
    if (!cann_crypto::shake::BuildLaunchConfig(request, 1U, config) ||
        !cann_crypto::detail::ValidBuffers(input, batch * inputLength, output, batch * outputLength)) {
        return ACL_ERROR_INVALID_PARAM;
    }
    uint32_t coreCount = 0;
    const aclError status = cann_crypto::detail::GetVectorCoreCount(coreCount);
    if (status != ACL_SUCCESS) {
        return status;
    }
    if (!cann_crypto::shake::BuildLaunchConfig(request, coreCount, config)) {
        return ACL_ERROR_INVALID_PARAM;
    }
    CannCryptoShakeLaunch(
        static_cast<uint32_t>(algorithm), config.blockDim, stream, input, output, config.batch, config.inputLength,
        config.outputLength, config.groupSize);
    return aclrtSynchronizeStream(stream);
}
} // namespace

extern "C" int32_t CannCryptoShake128(
    const uint8_t* input, uint8_t* output, uint64_t batch, uint64_t inputLength, uint64_t outputLength, void* stream)
{
    return RunShake(cann_crypto::shake::Algorithm::SHAKE128, input, output, batch, inputLength, outputLength, stream);
}

extern "C" int32_t CannCryptoShake256(
    const uint8_t* input, uint8_t* output, uint64_t batch, uint64_t inputLength, uint64_t outputLength, void* stream)
{
    return RunShake(cann_crypto::shake::Algorithm::SHAKE256, input, output, batch, inputLength, outputLength, stream);
}
