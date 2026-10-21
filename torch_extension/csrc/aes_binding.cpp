/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <ATen/Operators.h>
#include <torch/all.h>
#include <torch/library.h>
#include "torch_npu/csrc/core/npu/NPUStream.h"
#include "cann_crypto/aes.h"
#include "aad_host.h"
#include "tensor_utils.h"
#include <limits>

namespace {
constexpr uint32_t ALIGN = 512;
constexpr uint64_t MAX_INPUT_LENGTH = static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) - (ALIGN - 1);

uint32_t CheckedInputLength(const torch::Tensor& input)
{
    const int64_t size = input.numel();
    TORCH_CHECK(
        size <= static_cast<int64_t>(MAX_INPUT_LENGTH),
        "input length exceeds the maximum representable by the 32-bit aligned kernel metadata");
    return static_cast<uint32_t>(size);
}

torch::Tensor AesCtrMeta(const torch::Tensor& input, const torch::Tensor&, const torch::Tensor&)
{
    const uint32_t size = CheckedInputLength(input);
    const uint32_t aligned = (size + ALIGN - 1) / ALIGN * ALIGN;
    return torch::empty({static_cast<int64_t>(aligned)}, input.options().dtype(torch::kU8));
}

torch::Tensor AesGcmMeta(const torch::Tensor& input, const torch::Tensor&, const torch::Tensor&, const torch::Tensor&)
{
    const uint32_t size = CheckedInputLength(input);
    const uint32_t aligned = (size + ALIGN - 1) / ALIGN * ALIGN;
    return torch::empty({static_cast<int64_t>(aligned) + 16}, input.options().dtype(torch::kU8));
}

torch::Tensor AesCtrNpu(const torch::Tensor& input, const torch::Tensor& key, const torch::Tensor& nonce)
{
    const c10::OptionalDeviceGuard guard(input.device());
    const uint32_t size = CheckedInputLength(input);
    TORCH_CHECK(size > 0, "input length must be positive");
    auto output = AesCtrMeta(input, key, nonce);
    auto contiguous = input.contiguous();
    auto hostKey = ascend_ops::host::copyToCpuContiguous(key);
    auto hostNonce = ascend_ops::host::copyToCpuContiguous(nonce);
    TORCH_CHECK(hostKey.numel() >= 176, "key must be at least 176 bytes (44 uint32_t round keys)");
    TORCH_CHECK(hostNonce.numel() >= 12, "nonce must be at least 12 bytes");
    auto npuStream = c10_npu::getCurrentNPUStream();
    npuStream.synchronize(); // Flush queued Tensor operations before the direct launch.
    const int32_t status = CannCryptoAes128Ctr(
        contiguous.data_ptr<uint8_t>(), size, output.data_ptr<uint8_t>(), output.numel(), hostKey.data_ptr<uint8_t>(),
        hostNonce.data_ptr<uint8_t>(), npuStream.stream(false));
    TORCH_CHECK(status == 0, "AES-128-CTR failed with ACL status ", status);
    return output;
}

torch::Tensor AesGcmNpu(
    const torch::Tensor& input, const torch::Tensor& key, const torch::Tensor& nonce, const torch::Tensor& aad)
{
    const c10::OptionalDeviceGuard guard(input.device());
    const uint32_t size = CheckedInputLength(input);
    auto output = AesGcmMeta(input, key, nonce, aad);
    auto contiguous = input.contiguous();
    auto hostKey = ascend_ops::host::copyToCpuContiguous(key);
    auto hostNonce = ascend_ops::host::copyToCpuContiguous(nonce);
    TORCH_CHECK(hostKey.numel() >= 176, "key must be at least 176 bytes (44 uint32_t round keys)");
    TORCH_CHECK(hostNonce.numel() >= 12, "nonce must be at least 12 bytes");
    uint8_t hostAad[32]{};
    const uint32_t aadSize = ascend_ops::AesGcm::copy_gcm_aad_to_host(aad, hostAad, sizeof(hostAad));
    auto npuStream = c10_npu::getCurrentNPUStream();
    npuStream.synchronize();
    const int32_t status = CannCryptoAes128Gcm(
        contiguous.data_ptr<uint8_t>(), size, output.data_ptr<uint8_t>(), output.numel(), hostKey.data_ptr<uint8_t>(),
        hostNonce.data_ptr<uint8_t>(), hostAad, aadSize, npuStream.stream(false));
    TORCH_CHECK(status == 0, "AES-128-GCM failed with ACL status ", status);
    return output;
}
} // namespace

TORCH_LIBRARY_FRAGMENT(EXTENSION_MODULE_NAME, m)
{
    m.def("aes128ctr(Tensor input, Tensor key, Tensor nonce) -> Tensor");
    m.def("aes_gcm(Tensor input, Tensor key, Tensor nonce, Tensor aad) -> Tensor");
}
TORCH_LIBRARY_IMPL(EXTENSION_MODULE_NAME, Meta, m)
{
    m.impl("aes128ctr", AesCtrMeta);
    m.impl("aes_gcm", AesGcmMeta);
}
TORCH_LIBRARY_IMPL(EXTENSION_MODULE_NAME, PrivateUse1, m)
{
    m.impl("aes128ctr", AesCtrNpu);
    m.impl("aes_gcm", AesGcmNpu);
}
