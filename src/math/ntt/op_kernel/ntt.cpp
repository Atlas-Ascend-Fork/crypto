/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

// This entrypoint and its implementation belong to the NTT component.
#include "ntt_device.hpp"

extern "C" __global__ __aicore__ void ntt(
    GM_ADDR x, GM_ADDR matrixPhase1, GM_ADDR matrixPhase2, GM_ADDR twiddlePhase12, GM_ADDR z, GM_ADDR workspace,
    GM_ADDR tiling)
{
    REGISTER_TILING_DEFAULT(NttTilingData);
    GET_TILING_DATA_WITH_STRUCT(NttTilingData, tilingData, tiling);
    KERNEL_TASK_TYPE_DEFAULT(KERNEL_TYPE_MIX_AIC_1_2);
    if (workspace == nullptr) {
        return;
    }
    GM_ADDR userWorkspace = AscendC::GetUserWorkspace(workspace);
    if (userWorkspace == nullptr) {
        return;
    }
    CubeNtt op;
    op.Init(x, z, userWorkspace, matrixPhase1, matrixPhase2, twiddlePhase12, tilingData);
    op.Process();
}
