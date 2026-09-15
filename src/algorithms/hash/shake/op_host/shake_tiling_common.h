/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_CRYPTO_SHAKE_TILING_COMMON_H
#define CANN_CRYPTO_SHAKE_TILING_COMMON_H

#include "register/op_impl_registry.h"
#include "tiling/platform/platform_ascendc.h"
#include "../shake_internal.h"

namespace optiling {
inline bool BuildShakeTilingConfig(
    gert::TilingContext* context, cann_crypto::shake::Algorithm algorithm,
    cann_crypto::shake::LaunchConfig& config)
{
    if (context == nullptr || context->GetInputShape(0) == nullptr || context->GetOutputShape(0) == nullptr) {
        return false;
    }
    const auto& input = context->GetInputShape(0)->GetStorageShape();
    const auto& output = context->GetOutputShape(0)->GetStorageShape();
    if (input.GetDimNum() != 2U || output.GetDimNum() != 2U ||
        input.GetDim(0) <= 0 || input.GetDim(1) < 0 || output.GetDim(1) <= 0 ||
        input.GetDim(0) != output.GetDim(0)) {
        return false;
    }
    uint32_t coreCount = 1U;
    if (context->GetPlatformInfo() != nullptr) {
        platform_ascendc::PlatformAscendC platform(context->GetPlatformInfo());
        coreCount = static_cast<uint32_t>(platform.GetCoreNumAiv());
    }
    const cann_crypto::shake::Request request{algorithm,
        static_cast<uint64_t>(input.GetDim(0)), static_cast<uint64_t>(input.GetDim(1)),
        static_cast<uint64_t>(output.GetDim(1))};
    return cann_crypto::shake::BuildLaunchConfig(request, coreCount, config);
}

template<typename TilingData>
ge::graphStatus TilingShakeTensor(
    gert::TilingContext* context, cann_crypto::shake::Algorithm algorithm, uint64_t tilingKey)
{
    cann_crypto::shake::LaunchConfig config{};
    if (!BuildShakeTilingConfig(context, algorithm, config)) {
        return ge::GRAPH_FAILED;
    }
    auto* tiling = context->GetTilingData<TilingData>();
    if (tiling == nullptr || context->GetRawTilingData() == nullptr) {
        return ge::GRAPH_FAILED;
    }
    tiling->batch = config.batch;
    tiling->inputLength = config.inputLength;
    tiling->outputLength = config.outputLength;
    tiling->blockDim = config.blockDim;
    tiling->groupSize = config.groupSize;
    // 当前使用 uniform_u32 路径，保留字段置零。
    tiling->useFastPath = 0U;
    tiling->reserved0 = 0U;
    tiling->reserved1 = 0U;
    context->GetRawTilingData()->SetDataSize(sizeof(TilingData));
    context->SetBlockDim(config.blockDim);
    context->SetTilingKey(tilingKey);
    auto* workspace = context->GetWorkspaceSizes(1);
    if (workspace == nullptr) {
        return ge::GRAPH_FAILED;
    }
    workspace[0] = 0U;
    return ge::GRAPH_SUCCESS;
}
} // namespace optiling

#endif
