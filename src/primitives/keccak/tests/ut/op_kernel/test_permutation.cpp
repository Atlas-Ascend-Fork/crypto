/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "primitives/keccak/op_kernel/keccak_f1600_u32_vector.h"
#include "sha3_ref.h"

#include <cstdint>
#include <array>
#include <iostream>
#include <random>
#include <vector>

namespace {
bool TestXorSlots()
{
    using namespace KeccakF1600U32Vector;
    // 依次覆盖输出与左输入重合、与右输入重合、不重合及轮常量输入。
    constexpr std::array<std::array<uint32_t, 3>, 4> cases{{
        {{STATE_BASE, STATE_BASE, C_BASE}},
        {{STATE_BASE, C_BASE, STATE_BASE}},
        {{D_BASE, C_BASE, STATE_BASE}},
        {{STATE_BASE, STATE_BASE, TMP0}}
    }};
    for (const uint32_t count : {64U, 80U, 128U, 192U, 208U, 256U}) {
        for (const auto& slots : cases) {
            std::vector<uint32_t> memory(SLOT_COUNT * TILE);
            for (uint32_t i = 0; i < SLOT_COUNT * TILE; ++i) {
                memory[i] = (0x9e3779b9U * (i + 1U)) ^ 0xa5a5a5a5U;
            }
            Permutation permutation;
            for (uint32_t repeat = 0; repeat < 2U; ++repeat) {
                const auto before = memory;
                permutation.XorSlot(AscendC::LocalTensor<uint32_t>(memory.data()),
                    slots[0], slots[1], slots[2], count);
                for (uint32_t slot = 0; slot < SLOT_COUNT; ++slot) {
                    for (uint32_t i = 0; i < TILE; ++i) {
                        if (slot == TMP1 && i < count) {
                            continue;
                        }
                        const uint32_t expected = slot == slots[0] && i < count
                            ? before[slots[1] * TILE + i] ^ before[slots[2] * TILE + i]
                            : before[slot * TILE + i];
                        if (memory[slot * TILE + i] != expected) {
                            std::cerr << "XorSlot mismatch: " << count << ' ' << slot << ' ' << i << '\n';
                            return false;
                        }
                    }
                }
            }
        }
    }
    return true;
}
} // namespace

int main()
{
    using namespace KeccakF1600U32Vector;
    if (!TestXorSlots()) {
        return 1;
    }
    std::mt19937 random(20260909U);
    for (const uint32_t count : {64U, 80U, 128U, 192U, 208U, 256U}) {
        constexpr uint32_t sentinel = 0xdeadbeefU;
        std::vector<uint32_t> memory(SLOT_COUNT * TILE, sentinel);
        std::vector<uint64_t> expected(count * 25U);
        for (uint32_t message = 0; message < count; ++message) {
            for (uint32_t lane = 0; lane < 25U; ++lane) {
                const uint32_t low = random();
                const uint32_t high = random();
                memory[(2U * lane) * TILE + message] = low;
                memory[(2U * lane + 1U) * TILE + message] = high;
                expected[message * 25U + lane] = static_cast<uint64_t>(high) << 32U | low;
            }
            CannCryptoRefKeccakF1600(expected.data() + message * 25U);
        }
        Permutation permutation;
        permutation.Permute24(AscendC::LocalTensor<uint32_t>(memory.data()), count);
        for (uint32_t message = 0; message < count; ++message) {
            for (uint32_t lane = 0; lane < 25U; ++lane) {
                const uint64_t actual = static_cast<uint64_t>(memory[(2U * lane + 1U) * TILE + message]) << 32U |
                    memory[(2U * lane) * TILE + message];
                if (actual != expected[message * 25U + lane]) {
                    std::cerr << "permutation mismatch: " << count << ' ' << message << ' ' << lane << '\n';
                    return 1;
                }
            }
        }
        for (uint32_t slot = 0; slot < SLOT_COUNT; ++slot) {
            for (uint32_t message = count; message < TILE; ++message) {
                if (memory[slot * TILE + message] != sentinel) {
                    return 1;
                }
            }
        }
    }
    std::cout << "PERMUTATION_ARITHMETIC_MODEL=PASS\n";
    return 0;
}
