/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

// Optional SDK test: exercises real gert contexts, without launching an NPU kernel.
#include "ntt_internal.h"
#include "adapters/cann/ntt_adapter.h"
#include "base/context_builder/op_tiling_context_builder.h"

#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

using namespace cann_crypto::ntt;

bool Check(const NttParams& p, size_t tilingCapacity = sizeof(NttTilingData))
{
    const gert::StorageFormat format{ge::FORMAT_ND, ge::FORMAT_ND, {}};
    gert::Shape xShape{p.xElements / p.n, p.n};
    if (p.shapeRnsChannels > 0) {
        xShape = gert::Shape{p.shapeRnsChannels, p.xElements / p.n / p.shapeRnsChannels, p.n};
    }
    gert::StorageShape xStorage;
    xStorage.MutableOriginShape() = xShape;
    xStorage.MutableStorageShape() = xShape;
    gert::Tensor x(xStorage, format, ge::DT_INT32);
    gert::Tensor m1({{p.matrixPhase1Elements}, {p.matrixPhase1Elements}}, format, ge::DT_INT8);
    gert::Tensor m2({{p.matrixPhase2Elements}, {p.matrixPhase2Elements}}, format, ge::DT_INT8);
    gert::Tensor twiddle({{p.twiddleElements}, {p.twiddleElements}}, format, ge::DT_INT32);
    gert::Tensor y(xStorage, format, ge::DT_INT32);
    auto workspace = gert::ContinuousVector::Create<size_t>(1);
    // Ntt's compile info is empty and this adapter never reads platform data.
    // The SDK builder nevertheless requires non-null borrowed pointers.
    struct EmptyCompileInfo {
    } compileInfo;
    int platformToken = 0;
    gert::OpTilingContextBuilder builder;
    auto holder = builder.OpName("Ntt")
                      .OpType("Ntt")
                      .IONum(4, 1)
                      .CompileInfo(&compileInfo)
                      .PlatformInfo(&platformToken)
                      .InputTensors({&x, &m1, &m2, &twiddle})
                      .OutputTensors({&y})
                      .AppendAttr(p.blockDim)
                      .AppendAttr(p.subBench)
                      .AppendAttr(p.channelBlock)
                      .AppendAttr(p.q)
                      .AppendAttr(p.g)
                      .AppendAttr(p.logP)
                      .AppendAttr(p.rnsChannels)
                      .AppendAttr(std::vector<int64_t>(p.qList, p.qList + p.qListCount))
                      .AppendAttr(std::vector<int64_t>(p.gList, p.gList + p.gListCount))
                      .TilingDataSize(tilingCapacity)
                      .Workspace(reinterpret_cast<gert::ContinuousVector*>(workspace.get()))
                      .Build();
    auto* context = holder.GetContext();
    if (context == nullptr) {
        return false;
    }
    NttPlan plan;
    const auto nativeStatus = PrepareNttLaunch(p, plan);
    const auto cannStatus = optiling::NttTilingFunc(context);
    if (nativeStatus != NttStatus::Success || tilingCapacity < sizeof(NttTilingData)) {
        return cannStatus != ge::GRAPH_SUCCESS;
    }
    return cannStatus == ge::GRAPH_SUCCESS &&
           std::memcmp(context->GetTilingData<NttTilingData>(), &plan.tiling, sizeof(plan.tiling)) == 0 &&
           context->GetRawTilingData()->GetDataSize() == sizeof(plan.tiling) &&
           context->GetBlockDim() == plan.launchBlockDim && context->GetTilingKey() == plan.tilingKey &&
           context->GetWorkspaceSizes(1)[0] == plan.workspaceBytes;
}

int main()
{
    int failures = 0;
    int checks = 0;
    const auto check = [&](const NttParams& p, size_t capacity = sizeof(NttTilingData)) {
        ++checks;
        if (!Check(p, capacity)) {
            ++failures;
            std::cerr << "adapter case " << checks << " failed\n";
        }
    };
    NttParams p;
    p.n = 256;
    p.xElements = 4096 * 256;
    p.matrixPhase1Elements = 16 * 256 * 256;
    p.matrixPhase2Elements = p.matrixPhase1Elements;
    p.twiddleElements = 1;
    p.blockDim = 16;
    p.subBench = 16;
    p.q = 3329;
    p.g = 17;
    check(p);
    p.xElements = 0;
    check(p);
    p.xElements = 4096 * 256;
    p.q = 8380417;
    p.g = 1753;
    check(p);
    p.qListCount = 1;
    p.gListCount = 1;
    p.qList[0] = p.q;
    p.gList[0] = p.g;
    check(p);
    check(p, sizeof(NttTilingData) - 1);
    p.gList[0] = 1;
    check(p);
    p.gList[0] = p.g;
    p.qList[0] += (1LL << 32);
    check(p);
    p.qListCount = 0;
    p.gListCount = 0;
    p.blockDim = (1LL << 32) + 16;
    check(p);
    p.blockDim = 16;
    ++p.matrixPhase1Elements;
    check(p);
    p = {};
    p.n = 65536;
    p.xElements = 8 * 65536;
    p.shapeRnsChannels = 8;
    p.matrixPhase1Elements = 8 * 16 * 256 * 256;
    p.matrixPhase2Elements = p.matrixPhase1Elements;
    p.twiddleElements = 8 * 65536;
    p.blockDim = 16;
    p.subBench = 16;
    p.logP = 200;
    p.qListCount = 8;
    for (auto& q : p.qList) {
        q = 201326593;
    }
    for (int64_t lanes : {1, 2, 4}) {
        p.channelBlock = lanes;
        p.blockDim = 16 / lanes;
        check(p);
    }
    ++checks;
    if (optiling::NttTilingFunc(nullptr) != ge::GRAPH_FAILED) {
        ++failures;
    }
    std::cout << "NTT CANN adapter checks=" << checks << " failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
