/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_CRYPTO_NTT_INTERNAL_H
#define CANN_CRYPTO_NTT_INTERNAL_H

#include <cstddef>
#include <cstdint>
#include "op_kernel/ntt_tiling_data.h"

namespace cann_crypto::ntt {

// Internal C++17 interface, not the public stable C ABI. No CANN tensor types.
// Sizes are element counts, not byte counts. Matrices must already be B1Nz packed.
struct NttParams {
    int64_t n = 0;
    int64_t xElements = 0;
    int64_t matrixPhase1Elements = 0;
    int64_t matrixPhase2Elements = 0;
    int64_t twiddleElements = 0;
    int64_t shapeRnsChannels = 0; // First x dimension only when rank >= 3.
    int64_t blockDim = 10;
    int64_t subBench = 64;
    int64_t channelBlock = 1;
    int64_t q = 8380417;
    int64_t g = 10;
    int64_t logP = 0;
    int64_t rnsChannels = 0;
    int64_t qList[16]{};
    int64_t gList[16]{};
    size_t qListCount = 0;
    size_t gListCount = 0;
};

enum class NttStatus : int32_t {
    Success = 0,
    InvalidArgument = 1,
    InvalidShape = 2,
    BufferTooSmall = 3,
    BackendUnavailable = 4,
    LaunchFailed = 5,
};

enum class NttVariant : uint32_t { DirectBat4, FourStepBat4, KemBat2, DsaBat3 };

struct NttPlan {
    NttTilingData tiling{};
    size_t userWorkspaceBytes = 0;
    size_t workspaceBytes = 0; // Kernel scratch only; no framework workspace prefix.
    uint32_t launchBlockDim = 0;
    NttVariant variant = NttVariant::DirectBat4;
};

// Device addresses and byte capacities. The caller owns all buffers and the stream.
struct NttBuffer {
    void* data = nullptr;
    size_t bytes = 0;
};
struct NttBuffers {
    NttBuffer x, matrixPhase1, matrixPhase2, twiddlePhase12, y, workspace;
};

// A core/runtime backend must copy plan metadata before returning and enqueue the
// ntt kernel in the supplied stream, using the plan's tiling and block count.
// Device buffers must remain valid until stream completion; this API never syncs.
// No backend is supplied implicitly and a missing backend is an explicit error.
using NttEnqueue = NttStatus (*)(void* backendContext, const NttPlan& plan, const NttBuffers& buffers, void* stream);

NttStatus BuildNttTiling(
    const NttParams& params, NttTilingData& tiling, size_t& userWorkspaceBytes, const char** error = nullptr);
NttStatus PrepareNttLaunch(const NttParams& params, NttPlan& plan, const char** error = nullptr);
NttStatus LaunchNtt(
    const NttParams& params, const NttBuffers& buffers, NttEnqueue enqueue, void* backendContext, void* stream,
    const char** error = nullptr);

// Link cann_crypto_ntt_device for native Ascend C kernel launch. Tiling is copied
// into the launch arguments before return; device buffers and workspace remain
// caller-owned until the supplied stream completes. This wrapper never syncs.
NttStatus LaunchNtt(const NttParams& params, const NttBuffers& buffers, void* stream, const char** error = nullptr);

} // namespace cann_crypto::ntt
#endif
