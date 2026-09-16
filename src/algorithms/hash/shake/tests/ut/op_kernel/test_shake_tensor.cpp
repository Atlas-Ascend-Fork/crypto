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
#include "../../../../../../primitives/keccak/tests/ut/op_kernel/tiling_test_compat.h"
#if SHAKE_TEST_BITS == 128
#include "../../../op_kernel/shake128_tensor.cpp"
using TilingData = Shake128TensorTilingData;
#else
#include "../../../op_kernel/shake256_tensor.cpp"
using TilingData = Shake256TensorTilingData;
#endif
#include "sha3_ref.h"

#include <algorithm>
#include <new>
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
    GmBuffer workspace(static_cast<uint8_t*>(AscendC::GmAlloc(32)));
    std::unique_ptr<TilingData, GmDeleter> tiling(reinterpret_cast<TilingData*>(AscendC::GmAlloc(sizeof(TilingData))));
    ASSERT_NE(x.get(), nullptr);
    ASSERT_NE(y.get(), nullptr);
    ASSERT_NE(workspace.get(), nullptr);
    ASSERT_NE(tiling.get(), nullptr);
    for (size_t i = 0; i < inputBytes; ++i) {
        x.get()[i] = static_cast<uint8_t>(i * 37U + 11U);
    }
    std::fill_n(y.get(), outputBytes, 0);
    TilingData data{};
    data.batch = shape.batch;
    data.inputLength = shape.inputLength;
    data.outputLength = shape.outputLength;
    data.groupSize = 64U;
    data.blockDim = (shape.batch + 63U) / 64U;
    // 在 GM 原始存储中建立对象生命周期，再按类型赋值。
    ::new (static_cast<void*>(tiling.get())) TilingData{};
    *tiling.get() = data;
    ICPU_SET_TILING_KEY(0);
    AscendC::SetKernelMode(KernelMode::AIV_MODE);
#if SHAKE_TEST_BITS == 128
    ICPU_RUN_KF(
        shake128_tensor, data.blockDim, x.get(), y.get(), workspace.get(), reinterpret_cast<uint8_t*>(tiling.get()));
#else
    ICPU_RUN_KF(
        shake256_tensor, data.blockDim, x.get(), y.get(), workspace.get(), reinterpret_cast<uint8_t*>(tiling.get()));
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
