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
extern "C" {
void CannCryptoLaunchAesCtr(void* input, void* output, void* tiling, void* stream);
void CannCryptoLaunchAesGcmPrecompute(void* precomputed, void* tiling, void* stream);
void CannCryptoLaunchAesGcmEncrypt(uint32_t size, void* input, void* output, void* tiling, void* stream);
void CannCryptoLaunchAesGcmGhash(void* output, void* intermediate, void* precomputed, void* tiling, void* stream);
void CannCryptoLaunchAesGcmTag(void* output, void* intermediate, void* precomputed, void* tiling, void* stream);
}
