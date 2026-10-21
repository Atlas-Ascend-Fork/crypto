// -----------------------------------------------------------------------------------------------------------
// Copyright (c) 2026 Huawei Technologies Co., Ltd.
// This program is free software, you can redistribute it and/or modify it under the terms and conditions of
// CANN Open Software License Agreement Version 2.0 (the "License").
// Please refer to the License for details. You may not use this file except in compliance with the License.
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
// See LICENSE in the root of the software repository for the full text of the License.
// -----------------------------------------------------------------------------------------------------------

#ifndef CANN_CRYPTO_AES_GCM_AAD_HOST_H
#define CANN_CRYPTO_AES_GCM_AAD_HOST_H

#include <ATen/ATen.h>
#include <cstddef>
#include <cstdint>

namespace ascend_ops {
namespace AesGcm {

inline uint32_t copy_gcm_aad_to_host(const at::Tensor& aad, uint8_t* destination, size_t capacity)
{
    const auto aad_len = aad.numel();
    TORCH_CHECK(aad_len <= static_cast<int64_t>(capacity), "aad must be <= 32 bytes");

    auto aad_cpu = aad.cpu().contiguous();
    if (aad_len != 0) {
        const auto* aad_data = aad_cpu.data_ptr<uint8_t>();
        for (int64_t index = 0; index < aad_len; ++index) {
            destination[index] = aad_data[index];
        }
    }
    return static_cast<uint32_t>(aad_len);
}

} // namespace AesGcm
} // namespace ascend_ops

#endif // CANN_CRYPTO_AES_GCM_AAD_HOST_H
