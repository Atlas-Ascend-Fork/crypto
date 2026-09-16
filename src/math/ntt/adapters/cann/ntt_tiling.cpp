/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "../../ntt_internal.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include "ntt_adapter.h"

namespace optiling {
namespace {
using cann_crypto::ntt::NttParams;
using cann_crypto::ntt::NttPlan;
using cann_crypto::ntt::NttStatus;

struct NttCompileInfo {};

bool ShapeElements(const gert::StorageShape* storage, int64_t& result)
{
    if (storage == nullptr) {
        return false;
    }
    const auto& shape = storage->GetStorageShape();
    result = 1;
    for (size_t i = 0; i < shape.GetDimNum(); ++i) {
        const int64_t dim = shape.GetDim(i);
        if (dim == 0) {
            return false;
        }
        if (dim < 0) {
            return false;
        }
        if (result > std::numeric_limits<int64_t>::max() / dim) {
            return false;
        }
        result *= dim;
    }
    return true;
}

int64_t IntAttr(const gert::RuntimeAttrs* attrs, size_t index, int64_t fallback)
{
    const auto* value = attrs != nullptr ? attrs->GetInt(index) : nullptr;
    return value != nullptr ? *value : fallback;
}

bool ListAttr(const gert::RuntimeAttrs* attrs, size_t index, int64_t (&out)[16], size_t& count)
{
    const auto* values = attrs != nullptr ? attrs->GetListInt(index) : nullptr;
    count = values != nullptr ? values->GetSize() : 0;
    if (count > 16) {
        return false;
    }
    if (count != 0) {
        const auto* data = values->GetData();
        if (data == nullptr) {
            return false;
        }
        for (size_t i = 0; i < count; ++i) {
            out[i] = data[i];
        }
    }
    return true;
}

} // namespace

// CANN-specific decoding ends here; all validation, sizing, and routing live
// in the same neutral implementation used by the native internal entrypoint.
bool DecodeParameters(gert::TilingContext* context, NttParams& params)
{
    if (!ShapeElements(context->GetInputShape(0), params.xElements) ||
        !ShapeElements(context->GetInputShape(1), params.matrixPhase1Elements) ||
        !ShapeElements(context->GetInputShape(2), params.matrixPhase2Elements) ||
        !ShapeElements(context->GetInputShape(3), params.twiddleElements)) {
        return false;
    }
    const auto& shape = context->GetInputShape(0)->GetStorageShape();
    params.n = shape.GetDimNum() == 0 ? 1 : shape.GetDim(shape.GetDimNum() - 1);
    params.shapeRnsChannels = shape.GetDimNum() >= 3 ? shape.GetDim(0) : 0;
    const auto* attrs = context->GetAttrs();
    params.blockDim = IntAttr(attrs, 0, params.blockDim);
    params.subBench = IntAttr(attrs, 1, params.subBench);
    params.channelBlock = IntAttr(attrs, 2, params.channelBlock);
    params.q = IntAttr(attrs, 3, params.q);
    params.g = IntAttr(attrs, 4, params.g);
    params.logP = IntAttr(attrs, 5, params.logP);
    params.rnsChannels = IntAttr(attrs, 6, params.rnsChannels);
    if (!ListAttr(attrs, 7, params.qList, params.qListCount) || !ListAttr(attrs, 8, params.gList, params.gListCount)) {
        return false;
    }
    return true;
}

void TraceTiling(const NttPlan& plan)
{
    const char* trace = std::getenv("NTT_TRACE_TILING");
    if (trace != nullptr && std::strcmp(trace, "1") == 0) {
        std::fprintf(
            stderr, "[crypto Ntt tiling] N=%d q=%d batch=%d blockDim=%u subBench=%d workspace=%zu\n", plan.tiling.n,
            plan.tiling.q, plan.tiling.polyCount, plan.launchBlockDim, plan.tiling.subBench, plan.workspaceBytes);
    }
}

ge::graphStatus PublishTiling(gert::TilingContext* context, const NttPlan& plan)
{
    auto* tiling = context->GetTilingData<NttTilingData>();
    auto* rawTiling = context->GetRawTilingData();
    auto* workspace = context->GetWorkspaceSizes(1);
    if (tiling == nullptr || rawTiling == nullptr || workspace == nullptr ||
        rawTiling->GetCapacity() < sizeof(NttTilingData)) {
        return ge::GRAPH_FAILED;
    }
    *tiling = plan.tiling;
    rawTiling->SetDataSize(sizeof(plan.tiling));
    workspace[0] = plan.workspaceBytes;
    context->SetBlockDim(plan.launchBlockDim);
    context->SetTilingKey(plan.tilingKey);
    TraceTiling(plan);
    return ge::GRAPH_SUCCESS;
}

ge::graphStatus NttTilingFunc(gert::TilingContext* context)
{
    if (context == nullptr) {
        return ge::GRAPH_FAILED;
    }
    NttParams params;
    if (!DecodeParameters(context, params)) {
        return ge::GRAPH_FAILED;
    }
    NttPlan plan;
    const char* error = nullptr;
    if (cann_crypto::ntt::PrepareNttLaunch(params, plan, &error) != NttStatus::Success) {
        std::fprintf(stderr, "[Ntt tiling] %s\n", error != nullptr ? error : "invalid parameters");
        return ge::GRAPH_FAILED;
    }
    return PublishTiling(context, plan);
}

static ge::graphStatus TilingParseForNtt([[maybe_unused]] gert::TilingParseContext* context)
{
    return ge::GRAPH_SUCCESS;
}
IMPL_OP_OPTILING(Ntt).Tiling(NttTilingFunc).TilingParse<NttCompileInfo>(TilingParseForNtt);
} // namespace optiling
