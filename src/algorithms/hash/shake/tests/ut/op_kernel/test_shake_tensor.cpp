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
#include <cstring>
#include <memory>
#include <vector>

namespace {
struct GmDeleter {
    void operator()(uint8_t* pointer) const { if (pointer != nullptr) { AscendC::GmFree(pointer); } }
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
    GmBuffer tiling(static_cast<uint8_t*>(AscendC::GmAlloc(sizeof(TilingData))));
    ASSERT_NE(x.get(), nullptr);
    ASSERT_NE(y.get(), nullptr);
    ASSERT_NE(workspace.get(), nullptr);
    ASSERT_NE(tiling.get(), nullptr);
    for (size_t i = 0; i < inputBytes; ++i) { x.get()[i] = static_cast<uint8_t>(i * 37U + 11U); }
    std::memset(y.get(), 0, outputBytes);
    TilingData data{};
    data.batch = shape.batch;
    data.inputLength = shape.inputLength;
    data.outputLength = shape.outputLength;
    data.groupSize = 64U;
    data.blockDim = (shape.batch + 63U) / 64U;
    std::memcpy(tiling.get(), &data, sizeof(data));
    ICPU_SET_TILING_KEY(0);
    AscendC::SetKernelMode(KernelMode::AIV_MODE);
#if SHAKE_TEST_BITS == 128
    ICPU_RUN_KF(shake128_tensor, data.blockDim, x.get(), y.get(), workspace.get(), tiling.get());
#else
    ICPU_RUN_KF(shake256_tensor, data.blockDim, x.get(), y.get(), workspace.get(), tiling.get());
#endif
    std::vector<uint8_t> expected(shape.outputLength);
    for (uint32_t message = 0; message < shape.batch; ++message) {
        ASSERT_EQ(CannCryptoRefShake(SHAKE_TEST_BITS,
            x.get() + static_cast<size_t>(message) * shape.inputLength, shape.inputLength,
            expected.data(), expected.size()), 0);
        for (uint32_t byte = 0; byte < shape.outputLength; ++byte) {
            ASSERT_EQ(y.get()[static_cast<size_t>(message) * shape.outputLength + byte], expected[byte]);
        }
    }
}

constexpr uint32_t RATE = SHAKE_TEST_BITS == 128 ? 168U : 136U;
INSTANTIATE_TEST_SUITE_P(Shapes, ShakeTensorKernelTest, testing::Values(
    Shape{1, 0, 1}, Shape{17, 65, 33}, Shape{33, RATE + 1, RATE + 1},
    Shape{65, RATE * 2 + 1, RATE * 2 + 3}));
}
