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
#include "aclrtlaunch_ntt.h"

namespace cann_crypto::ntt {
namespace {
NttStatus EnqueueDirect(void*, const NttPlan& plan, const NttBuffers& buffers, void* stream)
{
    // The generated stub copies this POD into kernel arguments synchronously.
    NttTilingData tiling = plan.tiling;
    const auto status = ACLRT_LAUNCH_KERNEL(ntt)(
        plan.launchBlockDim, static_cast<aclrtStream>(stream), buffers.x.data, buffers.matrixPhase1.data,
        buffers.matrixPhase2.data, buffers.twiddlePhase12.data, buffers.y.data, buffers.workspace.data, &tiling);
    return status == ACL_SUCCESS ? NttStatus::Success : NttStatus::LaunchFailed;
}
} // namespace

NttStatus LaunchNtt(const NttParams& params, const NttBuffers& buffers, void* stream, const char** error)
{
    return LaunchNtt(params, buffers, EnqueueDirect, nullptr, stream, error);
}
} // namespace cann_crypto::ntt
