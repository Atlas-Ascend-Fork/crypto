/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#pragma once
#include <cstdint>

namespace ascend_ops::AesCtr {
struct Aes128CtrTilingData {
    uint32_t rd_key[44];
    uint8_t nonce[12];
    uint32_t input_length;
};
} // namespace ascend_ops::AesCtr

struct Aes128GcmTilingData {
    uint32_t rd_key[44];
    uint8_t nonce[12];
    uint32_t totalDataSize;
    uint32_t aad_len;
    uint8_t aad[32];
    uint32_t numCores;
    uint32_t ghashCoreNum;
};
