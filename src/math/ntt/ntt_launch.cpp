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

#include <algorithm>
#include <limits>

namespace cann_crypto::ntt {

NttStatus PrepareNttLaunch(const NttParams& params, NttPlan& plan, const char** error)
{
    plan = {};
    NttPlan result{};
    const auto status = BuildNttTiling(params, result.tiling, result.userWorkspaceBytes, error);
    if (status != NttStatus::Success) {
        return status;
    }
    const auto& t = result.tiling;
    result.workspaceBytes = result.userWorkspaceBytes;
    result.launchBlockDim = static_cast<uint32_t>(t.blockDim * std::min(t.channelBlock, t.rnsChannels));
    // All variants share the same directly launched mixed AIC:AIV=1:2 kernel.
    const bool directPqc =
        t.n == 256 && t.rnsChannels == 1 && t.channelBlock == 1 && t.blockDim == 16 && t.subBench == 16;
    if (directPqc && t.q == 3329) {
        result.variant = NttVariant::KemBat2;
    } else if (directPqc && t.q == 8380417) {
        result.variant = NttVariant::DsaBat3;
    } else {
        result.variant = t.useFourStep != 0 ? NttVariant::FourStepBat4 : NttVariant::DirectBat4;
    }
    plan = result;
    return NttStatus::Success;
}

NttStatus LaunchNtt(
    const NttParams& params, const NttBuffers& buffers, NttEnqueue enqueue, void* backendContext, void* stream,
    const char** error)
{
    NttPlan plan;
    const auto status = PrepareNttLaunch(params, plan, error);
    if (status != NttStatus::Success) {
        return status;
    }
    const auto fail = [&](NttStatus code, const char* message) {
        if (error != nullptr) {
            *error = message;
        }
        return code;
    };
    const NttBuffer inputs[] = {buffers.x, buffers.matrixPhase1, buffers.matrixPhase2, buffers.twiddlePhase12,
                                buffers.y, buffers.workspace};
    const size_t required[] = {static_cast<size_t>(params.xElements) * sizeof(int32_t),
                               static_cast<size_t>(params.matrixPhase1Elements),
                               static_cast<size_t>(params.matrixPhase2Elements),
                               static_cast<size_t>(params.twiddleElements) * sizeof(int32_t),
                               static_cast<size_t>(params.xElements) * sizeof(int32_t),
                               plan.workspaceBytes};
    for (size_t i = 0; i < 6; ++i) {
        if (inputs[i].data == nullptr) {
            return fail(NttStatus::InvalidArgument, "device buffer is null");
        }
        if (inputs[i].bytes < required[i]) {
            return fail(NttStatus::BufferTooSmall, "device buffer capacity is too small");
        }
        const uintptr_t address = reinterpret_cast<uintptr_t>(inputs[i].data);
        if (address > std::numeric_limits<uintptr_t>::max() - required[i]) {
            return fail(NttStatus::InvalidArgument, "device buffer address range overflows");
        }
    }
    // Read-only tables may alias each other. Writable output/workspace may not
    // overlap any input or each other; in-place NTT is not part of this contract.
    for (size_t i = 4; i < 6; ++i) {
        const uintptr_t begin = reinterpret_cast<uintptr_t>(inputs[i].data);
        for (size_t j = 0; j < i; ++j) {
            const uintptr_t other = reinterpret_cast<uintptr_t>(inputs[j].data);
            if (begin < other + required[j] && other < begin + required[i]) {
                return fail(NttStatus::InvalidArgument, "writable device buffers overlap");
            }
        }
    }
    if (enqueue == nullptr) {
        return fail(NttStatus::BackendUnavailable, "a runtime enqueue backend is required");
    }
    const auto launchStatus = enqueue(backendContext, plan, buffers, stream);
    if (launchStatus != NttStatus::Success) {
        return fail(launchStatus, "runtime backend failed to enqueue NTT");
    }
    return NttStatus::Success;
}
} // namespace cann_crypto::ntt
