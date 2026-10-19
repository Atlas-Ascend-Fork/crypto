/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <gtest/gtest.h>
#include "tikicpulib.h"
#include "kernel_operator.h"
#include "../../../op_kernel/keccak_f1600_tensor.cpp"
#include "sha3_ref.h"

#include <cstring>
#include <memory>

namespace {
struct GmDeleter {
    void operator()(uint8_t* pointer) const
    {
        if (pointer != nullptr) {
            AscendC::GmFree(pointer);
        }
    }
};
using GmBuffer = std::unique_ptr<uint8_t, GmDeleter>;

class KeccakF1600TensorKernelTest : public testing::TestWithParam<uint32_t> {};

TEST_P(KeccakF1600TensorKernelTest, MatchesIndependentReference)
{
    const uint32_t batch = GetParam();
    const size_t bytes = 50U * batch * sizeof(uint32_t);
    GmBuffer x(static_cast<uint8_t*>(AscendC::GmAlloc(bytes)));
    GmBuffer y(static_cast<uint8_t*>(AscendC::GmAlloc(bytes)));
    ASSERT_NE(x.get(), nullptr);
    ASSERT_NE(y.get(), nullptr);
    auto* input = reinterpret_cast<uint32_t*>(x.get());
    for (uint32_t word = 0; word < 50; ++word) {
        for (uint32_t message = 0; message < batch; ++message) {
            input[word * batch + message] = 0x9e3779b9U * (word + 1U) ^ message;
        }
    }
    std::memset(y.get(), 0, bytes);
    AscendC::SetKernelMode(KernelMode::AIV_MODE);
    const uint32_t blocks = (batch + 255U) / 256U;
    ICPU_RUN_KF(keccak_f1600_tensor, blocks, x.get(), y.get(), batch);
    const auto* output = reinterpret_cast<const uint32_t*>(y.get());
    for (uint32_t message = 0; message < batch; ++message) {
        uint64_t expected[25];
        for (uint32_t lane = 0; lane < 25; ++lane) {
            expected[lane] = static_cast<uint64_t>(input[(2U * lane) * batch + message]) |
                             (static_cast<uint64_t>(input[(2U * lane + 1U) * batch + message]) << 32U);
        }
        CannCryptoRefKeccakF1600(expected);
        for (uint32_t lane = 0; lane < 25; ++lane) {
            ASSERT_EQ(output[(2U * lane) * batch + message], static_cast<uint32_t>(expected[lane]));
            ASSERT_EQ(output[(2U * lane + 1U) * batch + message], static_cast<uint32_t>(expected[lane] >> 32U));
        }
    }
}

// 包含单块、对齐尾块和多块调度。
INSTANTIATE_TEST_SUITE_P(
    Batches, KeccakF1600TensorKernelTest, testing::Values(1U, 7U, 8U, 64U, 255U, 256U, 257U, 320U));
} // namespace
