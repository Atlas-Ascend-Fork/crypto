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
#if SHAKE_TEST_BITS == 128
#include "../../../op_kernel/shake128_tensor.cpp"
#else
#include "../../../op_kernel/shake256_tensor.cpp"
#endif
#include "sha3_ref.h"

#include <algorithm>
#include <memory>
#include <vector>

namespace {
struct GmDeleter {
    void operator()(void* pointer) const
    {
        if (pointer != nullptr) {
            AscendC::GmFree(static_cast<uint8_t*>(pointer));
        }
    }
};
using GmBuffer = std::unique_ptr<uint8_t, GmDeleter>;

struct Shape {
    uint32_t batch;
    uint32_t inputLength;
    uint32_t outputLength;
};
class ShakeTensorKernelTest : public testing::TestWithParam<Shape> {};

TEST_P(ShakeTensorKernelTest, MatchesIndependentReference)
{
    const Shape shape = GetParam();
    const size_t inputBytes = static_cast<size_t>(shape.batch) * shape.inputLength;
    const size_t outputBytes = static_cast<size_t>(shape.batch) * shape.outputLength;
    GmBuffer x(static_cast<uint8_t*>(AscendC::GmAlloc(std::max(inputBytes, size_t{32}))));
    GmBuffer y(static_cast<uint8_t*>(AscendC::GmAlloc(outputBytes)));
    ASSERT_NE(x.get(), nullptr);
    ASSERT_NE(y.get(), nullptr);
    for (size_t i = 0; i < inputBytes; ++i) {
        x.get()[i] = static_cast<uint8_t>(i * 37U + 11U);
    }
    std::fill_n(y.get(), outputBytes, 0);
    constexpr uint32_t groupSize = 64U;
    const uint32_t blockDim = (shape.batch + groupSize - 1U) / groupSize;
    AscendC::SetKernelMode(KernelMode::AIV_MODE);
#if SHAKE_TEST_BITS == 128
    ICPU_RUN_KF(
        shake128_tensor, blockDim, x.get(), y.get(), shape.batch, shape.inputLength, shape.outputLength, groupSize);
#else
    ICPU_RUN_KF(
        shake256_tensor, blockDim, x.get(), y.get(), shape.batch, shape.inputLength, shape.outputLength, groupSize);
#endif
    std::vector<uint8_t> expected(shape.outputLength);
    for (uint32_t message = 0; message < shape.batch; ++message) {
        ASSERT_EQ(
            CannCryptoRefShake(
                SHAKE_TEST_BITS, x.get() + static_cast<size_t>(message) * shape.inputLength, shape.inputLength,
                expected.data(), expected.size()),
            0);
        for (uint32_t byte = 0; byte < shape.outputLength; ++byte) {
            ASSERT_EQ(y.get()[static_cast<size_t>(message) * shape.outputLength + byte], expected[byte]);
        }
    }
}

constexpr uint32_t RATE = SHAKE_TEST_BITS == 128 ? 168U : 136U;
INSTANTIATE_TEST_SUITE_P(
    Shapes, ShakeTensorKernelTest,
    testing::Values(
        Shape{1, 0, 1}, Shape{17, 65, 33}, Shape{33, RATE + 1, RATE + 1}, Shape{65, RATE * 2 + 1, RATE * 2 + 3}));
} // namespace
