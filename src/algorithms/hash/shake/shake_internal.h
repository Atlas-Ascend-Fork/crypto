/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_CRYPTO_SHAKE_INTERNAL_H
#define CANN_CRYPTO_SHAKE_INTERNAL_H

#include <cstdint>

namespace cann_crypto::shake {
// 内部请求和调度配置不依赖 CANN 类型。
enum class Algorithm : uint32_t { SHAKE128 = 128U, SHAKE256 = 256U };

struct Request {
    Algorithm algorithm;
    uint64_t batch;
    uint64_t inputLength;
    uint64_t outputLength;
};

struct LaunchConfig {
    uint32_t batch;
    uint32_t inputLength;
    uint32_t outputLength;
    uint32_t rateBytes;
    uint32_t groupSize;
    uint32_t blockDim;
};

bool BuildLaunchConfig(const Request& request, uint32_t aivCoreCount, LaunchConfig& config);
} // namespace cann_crypto::shake
#endif
