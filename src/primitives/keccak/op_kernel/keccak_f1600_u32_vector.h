#pragma once

#include "kernel_operator.h"

namespace KeccakF1600U32Vector {

constexpr uint32_t TILE = 256;
constexpr uint32_t STATE_WORDS = 50;
constexpr uint32_t STATE_BASE = 0;
constexpr uint32_t B_BASE = 50;
constexpr uint32_t C_BASE = 100;
constexpr uint32_t D_BASE = 110;
constexpr uint32_t ROTL_LEFT_BASE = 120;
constexpr uint32_t ROTL_RIGHT_BASE = 122;
constexpr uint32_t TMP0 = 124;
constexpr uint32_t TMP1 = 125; // XorSlot 专用临时输出，不作为调用方的输入或输出。
constexpr uint32_t SLOT_COUNT = 126;

constexpr uint64_t RC[24] = {
    0x0000000000000001ULL, 0x0000000000008082ULL,
    0x800000000000808aULL, 0x8000000080008000ULL,
    0x000000000000808bULL, 0x0000000080000001ULL,
    0x8000000080008081ULL, 0x8000000000008009ULL,
    0x000000000000008aULL, 0x0000000000000088ULL,
    0x0000000080008009ULL, 0x000000008000000aULL,
    0x000000008000808bULL, 0x800000000000008bULL,
    0x8000000000008089ULL, 0x8000000000008003ULL,
    0x8000000000008002ULL, 0x8000000000000080ULL,
    0x000000000000800aULL, 0x800000008000000aULL,
    0x8000000080008081ULL, 0x8000000000008080ULL,
    0x0000000080000001ULL, 0x8000000080008008ULL,
};

constexpr int RHO[25] = {
     0,  1, 62, 28, 27,
    36, 44,  6, 55, 20,
     3, 10, 43, 25, 39,
    41, 45, 15, 21,  8,
    18,  2, 61, 56, 14,
};

class Permutation {
public:
    __aicore__ inline Permutation() {}

    __aicore__ inline AscendC::LocalTensor<uint32_t> Slot(
        const AscendC::LocalTensor<uint32_t>& base,
        uint32_t slot)
    {
        return base[slot * TILE];
    }

    __aicore__ inline uint32_t LanePart(uint32_t lane, uint32_t part)
    {
        return lane * 2U + part;
    }

    __aicore__ inline void CopySlot(
        const AscendC::LocalTensor<uint32_t>& base,
        uint32_t dstSlot,
        uint32_t srcSlot,
        uint32_t n)
    {
        AscendC::DataCopy(Slot(base, dstSlot), Slot(base, srcSlot), n);
    }

    __aicore__ inline void XorSlot(
        const AscendC::LocalTensor<uint32_t>& base,
        uint32_t dstSlot,
        uint32_t src0Slot,
        uint32_t src1Slot,
        uint32_t n)
    {
        const bool overlapsInput = dstSlot == src0Slot || dstSlot == src1Slot;
        const uint32_t xorDstSlot = overlapsInput ? TMP1 : dstSlot;
        AscendC::Xor(
            Slot(base, xorDstSlot).ReinterpretCast<uint16_t>(),
            Slot(base, src0Slot).ReinterpretCast<uint16_t>(),
            Slot(base, src1Slot).ReinterpretCast<uint16_t>(),
            n * 2U);
        if (overlapsInput) {
            // 保留源数据直至异或完成；回写完成后才允许下一次复用临时槽。
            AscendC::PipeBarrier<PIPE_V>();
            CopySlot(base, dstSlot, TMP1, n);
            AscendC::PipeBarrier<PIPE_V>();
        }
    }

    __aicore__ inline void AndSlot(
        const AscendC::LocalTensor<uint32_t>& base,
        uint32_t dstSlot,
        uint32_t src0Slot,
        uint32_t src1Slot,
        uint32_t n)
    {
        AscendC::And(
            Slot(base, dstSlot).ReinterpretCast<uint16_t>(),
            Slot(base, src0Slot).ReinterpretCast<uint16_t>(),
            Slot(base, src1Slot).ReinterpretCast<uint16_t>(),
            n * 2U);
    }

    __aicore__ inline void OrSlot(
        const AscendC::LocalTensor<uint32_t>& base,
        uint32_t dstSlot,
        uint32_t src0Slot,
        uint32_t src1Slot,
        uint32_t n)
    {
        AscendC::Or(
            Slot(base, dstSlot).ReinterpretCast<uint16_t>(),
            Slot(base, src0Slot).ReinterpretCast<uint16_t>(),
            Slot(base, src1Slot).ReinterpretCast<uint16_t>(),
            n * 2U);
    }

    __aicore__ inline void RotlLaneTo(
        const AscendC::LocalTensor<uint32_t>& base,
        uint32_t dstBase,
        uint32_t srcBase,
        uint32_t rot,
        uint32_t n)
    {
        const uint32_t wordShift = rot / 32U;
        const uint32_t bitShift = rot % 32U;

        if (bitShift == 0U) {
            for (uint32_t p = 0; p < 2; ++p) {
                const uint32_t srcLo = (p + 2U - wordShift) & 1U;
                CopySlot(base, dstBase + p, srcBase + srcLo, n);
            }
            AscendC::PipeBarrier<PIPE_V>();
            return;
        }

        const uint32_t shl = bitShift;
        const uint32_t shr = 32U - bitShift;

        for (uint32_t p = 0; p < 2; ++p) {
            const uint32_t srcLo = (p + 2U - wordShift) & 1U;
            const uint32_t srcHi = (p + 2U - wordShift - 1U) & 1U;

            AscendC::ShiftLeft(Slot(base, ROTL_LEFT_BASE + p), Slot(base, srcBase + srcLo), shl, n);
            AscendC::ShiftRight(Slot(base, ROTL_RIGHT_BASE + p), Slot(base, srcBase + srcHi), shr, n);
        }
        AscendC::PipeBarrier<PIPE_V>();

        for (uint32_t p = 0; p < 2; ++p) {
            OrSlot(base, dstBase + p, ROTL_LEFT_BASE + p, ROTL_RIGHT_BASE + p, n);
        }
        AscendC::PipeBarrier<PIPE_V>();
    }

    __aicore__ inline void Round(
        const AscendC::LocalTensor<uint32_t>& base,
        uint32_t round,
        uint32_t n)
    {
        // θ：按列计算奇偶校验。高低两个 U32 部分独立，整列完成后同步。
        for (uint32_t x = 0; x < 5; ++x) {
            for (uint32_t p = 0; p < 2; ++p) {
                uint32_t cSlot = C_BASE + x * 2U + p;
                CopySlot(base, cSlot, STATE_BASE + LanePart(x, p), n);

                for (uint32_t y = 1; y < 5; ++y) {
                    uint32_t lane = x + 5U * y;
                    XorSlot(base, cSlot, cSlot, STATE_BASE + LanePart(lane, p), n);
                }
            }
            AscendC::PipeBarrier<PIPE_V>();
        }

        // D[x] = C[x-1] ^ ROTL1(C[x+1])。固定移位 1 位，直接组合高低字。
        for (uint32_t x = 0; x < 5; ++x) {
            const uint32_t next = (x + 1U) % 5U;
            const uint32_t cLo = C_BASE + next * 2U;
            const uint32_t cHi = C_BASE + next * 2U + 1U;

            AscendC::ShiftLeft(Slot(base, ROTL_LEFT_BASE), Slot(base, cLo), static_cast<uint32_t>(1), n);
            AscendC::ShiftRight(Slot(base, ROTL_RIGHT_BASE), Slot(base, cHi), static_cast<uint32_t>(31), n);
            AscendC::ShiftLeft(Slot(base, ROTL_LEFT_BASE + 1U), Slot(base, cHi), static_cast<uint32_t>(1), n);
            AscendC::ShiftRight(Slot(base, ROTL_RIGHT_BASE + 1U), Slot(base, cLo), static_cast<uint32_t>(31), n);
            AscendC::PipeBarrier<PIPE_V>();

            OrSlot(base, D_BASE + x * 2U, ROTL_LEFT_BASE, ROTL_RIGHT_BASE, n);
            OrSlot(base, D_BASE + x * 2U + 1U, ROTL_LEFT_BASE + 1U, ROTL_RIGHT_BASE + 1U, n);
            AscendC::PipeBarrier<PIPE_V>();
        }

        for (uint32_t x = 0; x < 5; ++x) {
            const uint32_t prev = (x + 4U) % 5U;
            for (uint32_t p = 0; p < 2; ++p) {
                XorSlot(base, D_BASE + x * 2U + p, D_BASE + x * 2U + p, C_BASE + prev * 2U + p, n);
            }
        }
        AscendC::PipeBarrier<PIPE_V>();

        // 将 D 异或到各 lane；写入位置互不重叠，完成后统一同步。
        for (uint32_t y = 0; y < 5; ++y) {
            for (uint32_t x = 0; x < 5; ++x) {
                const uint32_t lane = x + 5U * y;
                for (uint32_t p = 0; p < 2; ++p) {
                    XorSlot(base,
                            STATE_BASE + LanePart(lane, p),
                            STATE_BASE + LanePart(lane, p),
                            D_BASE + x * 2U + p,
                            n);
                }
            }
        }
        AscendC::PipeBarrier<PIPE_V>();

        // ρ 与 π：先循环移位，再按固定映射写入 B。
        // 静态展开映射，减少运行时坐标与旋转表索引计算。
        RotlLaneTo(base, B_BASE + LanePart(0U, 0), STATE_BASE + LanePart(0U, 0), 0U, n);
        RotlLaneTo(base, B_BASE + LanePart(10U, 0), STATE_BASE + LanePart(1U, 0), 1U, n);
        RotlLaneTo(base, B_BASE + LanePart(20U, 0), STATE_BASE + LanePart(2U, 0), 62U, n);
        RotlLaneTo(base, B_BASE + LanePart(5U, 0), STATE_BASE + LanePart(3U, 0), 28U, n);
        RotlLaneTo(base, B_BASE + LanePart(15U, 0), STATE_BASE + LanePart(4U, 0), 27U, n);
        RotlLaneTo(base, B_BASE + LanePart(16U, 0), STATE_BASE + LanePart(5U, 0), 36U, n);
        RotlLaneTo(base, B_BASE + LanePart(1U, 0), STATE_BASE + LanePart(6U, 0), 44U, n);
        RotlLaneTo(base, B_BASE + LanePart(11U, 0), STATE_BASE + LanePart(7U, 0), 6U, n);
        RotlLaneTo(base, B_BASE + LanePart(21U, 0), STATE_BASE + LanePart(8U, 0), 55U, n);
        RotlLaneTo(base, B_BASE + LanePart(6U, 0), STATE_BASE + LanePart(9U, 0), 20U, n);
        RotlLaneTo(base, B_BASE + LanePart(7U, 0), STATE_BASE + LanePart(10U, 0), 3U, n);
        RotlLaneTo(base, B_BASE + LanePart(17U, 0), STATE_BASE + LanePart(11U, 0), 10U, n);
        RotlLaneTo(base, B_BASE + LanePart(2U, 0), STATE_BASE + LanePart(12U, 0), 43U, n);
        RotlLaneTo(base, B_BASE + LanePart(12U, 0), STATE_BASE + LanePart(13U, 0), 25U, n);
        RotlLaneTo(base, B_BASE + LanePart(22U, 0), STATE_BASE + LanePart(14U, 0), 39U, n);
        RotlLaneTo(base, B_BASE + LanePart(23U, 0), STATE_BASE + LanePart(15U, 0), 41U, n);
        RotlLaneTo(base, B_BASE + LanePart(8U, 0), STATE_BASE + LanePart(16U, 0), 45U, n);
        RotlLaneTo(base, B_BASE + LanePart(18U, 0), STATE_BASE + LanePart(17U, 0), 15U, n);
        RotlLaneTo(base, B_BASE + LanePart(3U, 0), STATE_BASE + LanePart(18U, 0), 21U, n);
        RotlLaneTo(base, B_BASE + LanePart(13U, 0), STATE_BASE + LanePart(19U, 0), 8U, n);
        RotlLaneTo(base, B_BASE + LanePart(14U, 0), STATE_BASE + LanePart(20U, 0), 18U, n);
        RotlLaneTo(base, B_BASE + LanePart(24U, 0), STATE_BASE + LanePart(21U, 0), 2U, n);
        RotlLaneTo(base, B_BASE + LanePart(9U, 0), STATE_BASE + LanePart(22U, 0), 61U, n);
        RotlLaneTo(base, B_BASE + LanePart(19U, 0), STATE_BASE + LanePart(23U, 0), 56U, n);
        RotlLaneTo(base, B_BASE + LanePart(4U, 0), STATE_BASE + LanePart(24U, 0), 14U, n);

        // χ：使用等价式 B[x] ^ B[x+2] ^ (B[x+1] & B[x+2])。
        // 按行处理，复用在 ρ/π 后已经不再使用的 C、D 槽位。
        for (uint32_t y = 0; y < 5; ++y) {
            for (uint32_t x = 0; x < 5; ++x) {
                const uint32_t lane0 = x + 5U * y;
                const uint32_t lane1 = ((x + 1U) % 5U) + 5U * y;
                const uint32_t lane2 = ((x + 2U) % 5U) + 5U * y;

                for (uint32_t p = 0; p < 2; ++p) {
                    const uint32_t temp = x * 2U + p;
                    const uint32_t b0 = B_BASE + LanePart(lane0, p);
                    const uint32_t b1 = B_BASE + LanePart(lane1, p);
                    const uint32_t b2 = B_BASE + LanePart(lane2, p);

                    AndSlot(base, C_BASE + temp, b1, b2, n);
                    XorSlot(base, D_BASE + temp, b0, b2, n);
                }
            }
            AscendC::PipeBarrier<PIPE_V>();

            for (uint32_t x = 0; x < 5; ++x) {
                const uint32_t lane0 = x + 5U * y;
                for (uint32_t p = 0; p < 2; ++p) {
                    const uint32_t temp = x * 2U + p;
                    const uint32_t out = STATE_BASE + LanePart(lane0, p);
                    XorSlot(base, out, D_BASE + temp, C_BASE + temp, n);
                }
            }
            AscendC::PipeBarrier<PIPE_V>();
        }

        // ι：仅第一个 lane 异或当前轮常量。
        const uint64_t rc = RC[round];
        for (uint32_t p = 0; p < 2; ++p) {
            const uint32_t rcPart = static_cast<uint32_t>((rc >> (32U * p)) & 0xffffffffULL);
            if (rcPart != 0U) {
                AscendC::Duplicate(Slot(base, TMP0), rcPart, n);
                AscendC::PipeBarrier<PIPE_V>();
                XorSlot(base, STATE_BASE + p, STATE_BASE + p, TMP0, n);
                AscendC::PipeBarrier<PIPE_V>();
            }
        }
    }

    __aicore__ inline void Permute24(
        const AscendC::LocalTensor<uint32_t>& base,
        uint32_t n)
    {
        for (uint32_t round = 0; round < 24U; ++round) {
            Round(base, round, n);
        }
    }
};

}  // namespace KeccakF1600U32Vector
