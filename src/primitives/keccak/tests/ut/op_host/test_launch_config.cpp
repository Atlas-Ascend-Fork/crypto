/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "../../../keccak_internal.h"

#include <iostream>

int main()
{
    cann_crypto::keccak::LaunchConfig config{};
    for (uint32_t batch : {1U, 7U, 8U, 9U, 64U, 255U, 256U, 257U, 8191U, 8192U}) {
        for (uint32_t cores : {1U, 20U, 40U, 48U}) {
            if (!cann_crypto::keccak::BuildLaunchConfig(batch, cores, config)) {
                return 1;
            }
            const uint32_t tiles = (batch + 255U) / 256U;
            if (config.batch != batch || config.blockDim != (tiles < cores ? tiles : cores)) {
                return 1;
            }
        }
    }
    for (uint64_t batch : {uint64_t{0}, uint64_t{8193}, uint64_t{0x100000000ULL}, UINT64_MAX}) {
        if (cann_crypto::keccak::BuildLaunchConfig(batch, 40U, config)) {
            return 1;
        }
    }
    if (cann_crypto::keccak::BuildLaunchConfig(1U, 0U, config)) {
        return 1;
    }
    std::cout << "KECCAK_LAUNCH_CONFIG=PASS\n";
    return 0;
}
