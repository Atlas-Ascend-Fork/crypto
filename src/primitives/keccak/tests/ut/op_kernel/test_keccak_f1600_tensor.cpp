#include <gtest/gtest.h>
#include "tikicpulib.h"
#include "kernel_operator.h"
#include "tiling_test_compat.h"
#include "../../../op_kernel/keccak_f1600_tensor.cpp"
#include "sha3_ref.h"

#include <cstring>
#include <memory>

namespace {
struct GmDeleter {
    void operator()(uint8_t* pointer) const { if (pointer != nullptr) { AscendC::GmFree(pointer); } }
};
using GmBuffer = std::unique_ptr<uint8_t, GmDeleter>;

class KeccakF1600TensorKernelTest : public testing::TestWithParam<uint32_t> {};

TEST_P(KeccakF1600TensorKernelTest, MatchesIndependentReference)
{
    const uint32_t batch = GetParam();
    const size_t bytes = 50U * batch * sizeof(uint32_t);
    GmBuffer x(static_cast<uint8_t*>(AscendC::GmAlloc(bytes)));
    GmBuffer y(static_cast<uint8_t*>(AscendC::GmAlloc(bytes)));
    GmBuffer workspace(static_cast<uint8_t*>(AscendC::GmAlloc(32)));
    GmBuffer tiling(static_cast<uint8_t*>(AscendC::GmAlloc(sizeof(KeccakF1600TensorTilingData))));
    ASSERT_NE(x.get(), nullptr);
    ASSERT_NE(y.get(), nullptr);
    ASSERT_NE(workspace.get(), nullptr);
    ASSERT_NE(tiling.get(), nullptr);
    auto* input = reinterpret_cast<uint32_t*>(x.get());
    for (uint32_t word = 0; word < 50; ++word) {
        for (uint32_t message = 0; message < batch; ++message) {
            input[word * batch + message] = 0x9e3779b9U * (word + 1U) ^ message;
        }
    }
    std::memset(y.get(), 0, bytes);
    const KeccakF1600TensorTilingData data{batch};
    std::memcpy(tiling.get(), &data, sizeof(data));
    ICPU_SET_TILING_KEY(0);
    AscendC::SetKernelMode(KernelMode::AIV_MODE);
    const uint32_t blocks = (batch + 255U) / 256U;
    ICPU_RUN_KF(keccak_f1600_tensor, blocks, x.get(), y.get(), workspace.get(), tiling.get());
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
INSTANTIATE_TEST_SUITE_P(Batches, KeccakF1600TensorKernelTest, testing::Values(64U, 256U, 320U));
}
