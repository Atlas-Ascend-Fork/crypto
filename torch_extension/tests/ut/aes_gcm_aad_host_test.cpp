// -----------------------------------------------------------------------------------------------------------
// Copyright (c) 2026 Huawei Technologies Co., Ltd.
// This program is free software, you can redistribute it and/or modify it under the terms and conditions of
// CANN Open Software License Agreement Version 2.0 (the "License").
// Please refer to the License for details. You may not use this file except in compliance with the License.
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
// See LICENSE in the root of the software repository for the full text of the License.
// -----------------------------------------------------------------------------------------------------------

#include "torch_extension/csrc/aad_host.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {
constexpr size_t kAadSize = 32;

void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

} // namespace

int main()
{
    auto base = at::arange(0, static_cast<int64_t>(kAadSize), at::TensorOptions().dtype(at::kByte)).reshape({4, 8});
    auto transposed = base.transpose(0, 1);
    require(transposed.sizes() == at::IntArrayRef({8, 4}), "transposed AAD shape mismatch");
    require(transposed.strides() == at::IntArrayRef({1, 8}), "transposed AAD stride mismatch");
    require(!transposed.is_contiguous(), "transposed AAD unexpectedly contiguous");

    const std::array<uint8_t, kAadSize> expected_logical = {
        0, 8,  16, 24, 1, 9,  17, 25, 2, 10, 18, 26, 3, 11, 19, 27,
        4, 12, 20, 28, 5, 13, 21, 29, 6, 14, 22, 30, 7, 15, 23, 31,
    };
    std::array<uint8_t, kAadSize> copied{};
    const auto copied_len = ascend_ops::AesGcm::copy_gcm_aad_to_host(transposed, copied.data(), copied.size());
    require(copied_len == kAadSize, "strided AAD length mismatch");
    require(copied == expected_logical, "strided AAD was not copied in logical tensor order");

    auto contiguous = transposed.contiguous();
    std::array<uint8_t, kAadSize> contiguous_copy{};
    const auto contiguous_len =
        ascend_ops::AesGcm::copy_gcm_aad_to_host(contiguous, contiguous_copy.data(), contiguous_copy.size());
    require(contiguous_len == kAadSize, "contiguous AAD length mismatch");
    require(contiguous_copy == expected_logical, "contiguous AAD control changed bytes");

    std::array<uint8_t, kAadSize> old_physical_order{};
    const auto* physical_data = transposed.data_ptr<uint8_t>();
    for (size_t index = 0; index < old_physical_order.size(); ++index) {
        old_physical_order[index] = physical_data[index];
    }
    require(
        old_physical_order != expected_logical,
        "regression input does not distinguish physical storage order from logical tensor order");
    return 0;
}
