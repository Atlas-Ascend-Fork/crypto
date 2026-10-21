/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_CRYPTO_KECCAK_INTERNAL_H
#define CANN_CRYPTO_KECCAK_INTERNAL_H

#include <cstdint>

namespace cann_crypto::keccak {
struct LaunchConfig {
    uint32_t batch;
    uint32_t blockDim;
};

inline bool BuildLaunchConfig(uint64_t batch, uint32_t coreCount, LaunchConfig& config)
{
    if (batch == 0U || batch > 8192U || coreCount == 0U) {
        return false;
    }
    const auto count = static_cast<uint32_t>(batch);
    const uint32_t tiles = (count + 255U) / 256U;
    config = {count, tiles < coreCount ? tiles : coreCount};
    return true;
}
} // namespace cann_crypto::keccak
#endif
