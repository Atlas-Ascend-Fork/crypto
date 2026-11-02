/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ntt_internal.h"
#include "acl/acl.h"

extern "C" aclError CannCryptoNttLaunch(
    uint32_t blockDim, void* stream, void* x, void* matrixPhase1, void* matrixPhase2, void* twiddlePhase12, void* y,
    void* workspace, NttTilingData tiling);

namespace cann_crypto::ntt {
namespace {
NttStatus EnqueueDirect(void*, const NttPlan& plan, const NttBuffers& buffers, void* stream)
{
    const auto status = CannCryptoNttLaunch(
        plan.launchBlockDim, stream, buffers.x.data, buffers.matrixPhase1.data, buffers.matrixPhase2.data,
        buffers.twiddlePhase12.data, buffers.y.data, buffers.workspace.data, plan.tiling);
    return status == ACL_SUCCESS ? NttStatus::Success : NttStatus::LaunchFailed;
}
} // namespace

NttStatus LaunchNtt(const NttParams& params, const NttBuffers& buffers, void* stream, const char** error)
{
    return LaunchNtt(params, buffers, EnqueueDirect, nullptr, stream, error);
}
} // namespace cann_crypto::ntt
