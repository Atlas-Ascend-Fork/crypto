/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#include "kernel_operator.h"
#include "../../../../primitives/sm3/op_kernel/sm3_vector_internal.h"
using namespace AscendC;
constexpr uint32_t Q = SM3_VECTOR_WIDTH;
static_assert(Q == 32 || Q == 64 || Q == 128 || Q == 256, "validated vector tile candidates");
namespace {
using Sm3 = cann_crypto::sm3::device::VectorSm3<Q>;

__aicore__ inline void ExpandPadding(uint32_t length, uint32_t* padding)
{
    for (uint32_t j = 0; j < 16; ++j)
        padding[j] = 0;
    const uint64_t bits = uint64_t(length) * 8;
    padding[0] = 0x80000000U;
    padding[14] = uint32_t(bits >> 32);
    padding[15] = uint32_t(bits);
    for (uint32_t j = 16; j < 68; ++j) {
        using cann_crypto::sm3::device::RotateLeft;
        const uint32_t x = padding[j - 16] ^ padding[j - 9] ^ RotateLeft(padding[j - 3], 15);
        padding[j] = x ^ RotateLeft(x, 15) ^ RotateLeft(x, 23) ^ RotateLeft(padding[j - 13], 7) ^ padding[j - 6];
    }
}

struct VectorBatch {
    TPipe& pipe;
    Sm3& sm;
    GlobalTensor<uint8_t>& input;
    GlobalTensor<uint32_t>& input32;
    GlobalTensor<uint8_t>& output;
    LocalTensor<uint32_t>& incoming;
    LocalTensor<uint32_t>& offsets;
    LocalTensor<uint32_t>& outputOffsets;
    LocalTensor<uint32_t>& out32;
    LocalTensor<uint8_t>& outgoing;
    uint32_t length;
    uint32_t totalBlocks;
    bool dmaAligned;

    __aicore__ inline void ClearIncoming()
    {
        Duplicate(incoming, uint32_t(0), Q * 16);
        PipeBarrier<PIPE_V>();
        const auto clear = pipe.FetchEventID(HardEvent::V_MTE2);
        SetFlag<HardEvent::V_MTE2>(clear);
        WaitFlag<HardEvent::V_MTE2>(clear);
    }

    __aicore__ inline void CopyAligned(uint32_t offset, uint32_t active)
    {
        if (active < Q)
            ClearIncoming();
        const auto free = pipe.FetchEventID(HardEvent::S_MTE2);
        SetFlag<HardEvent::S_MTE2>(free);
        WaitFlag<HardEvent::S_MTE2>(free);
        DataCopy(incoming, input32[offset / 4], DataCopyParams{uint16_t(active), 2, uint16_t((length - 64) / 32), 0});
        const auto ready = pipe.FetchEventID(HardEvent::MTE2_S);
        SetFlag<HardEvent::MTE2_S>(ready);
        WaitFlag<HardEvent::MTE2_S>(ready);
    }

    __aicore__ inline void CopyPadded(uint32_t offset, uint32_t remaining, uint32_t active)
    {
        const uint32_t copyBytes = remaining < 64 ? remaining : 64;
        const uint32_t paddedBytes = (copyBytes + 31) & ~uint32_t(31);
        ClearIncoming();
        DataCopyPad(
            incoming.ReinterpretCast<uint8_t>(), input[offset],
            DataCopyExtParams{uint16_t(active), copyBytes, length - copyBytes, (64 - paddedBytes) / 32, 0},
            DataCopyPadExtParams<uint8_t>{true, 0, uint8_t(paddedBytes - copyBytes), uint8_t(0)});
    }

    __aicore__ inline uint32_t LengthWord(uint32_t word) const
    {
        const uint64_t bits = uint64_t(length) * 8;
        return word == 14 ? uint32_t(bits >> 32) : uint32_t(bits);
    }

    __aicore__ inline void LoadPadding(uint32_t block)
    {
        for (uint32_t word = 0; word < 16; ++word) {
            uint32_t value = block * 64 + word * 4 == length ? 0x80000000U : 0;
            if (block + 1 == totalBlocks && word >= 14)
                value = LengthWord(word);
            sm.v.Constant(sm.W(word), value);
        }
    }

    __aicore__ inline void AddPadding(uint32_t block, uint32_t word)
    {
        const uint32_t position = block * 64 + word * 4;
        if (position <= length && length < position + 4) {
            sm.v.Constant(sm.T(0), 0x80U << (24 - 8 * (length % 4)));
            sm.v.Or(sm.W(word), sm.W(word), sm.T(0));
        }
        if (block + 1 == totalBlocks && word >= 14)
            sm.v.Constant(sm.W(word), LengthWord(word));
    }

    __aicore__ inline void LoadBlock(uint32_t base, uint32_t block, uint32_t active)
    {
        const uint32_t position = block * 64;
        if (position >= length) {
            LoadPadding(block);
            return;
        }
        const uint32_t offset = base * length + position;
        const bool aligned = dmaAligned && position + 64 <= length;
        if (aligned)
            CopyAligned(offset, active);
        else
            CopyPadded(offset, length - position, active);
        const auto loaded = pipe.FetchEventID(HardEvent::MTE2_V);
        SetFlag<HardEvent::MTE2_V>(loaded);
        WaitFlag<HardEvent::MTE2_V>(loaded);
        for (uint32_t word = 0; word < 16; ++word) {
            Gather(sm.W(word), incoming, offsets, word * 4, Q);
            sm.v.Barrier();
            sm.v.ByteSwap(sm.W(word), sm.W(word));
            if (!aligned)
                AddPadding(block, word);
        }
    }

    __aicore__ inline void CompressBlock(uint32_t base, uint32_t block, uint32_t active, const uint32_t* padding)
    {
        const bool useFixedPadding = padding && block + 1 == totalBlocks;
        if (!useFixedPadding)
            LoadBlock(base, block, active);
        const auto ready = pipe.FetchEventID(HardEvent::S_V);
        SetFlag<HardEvent::S_V>(ready);
        WaitFlag<HardEvent::S_V>(ready);
        if (useFixedPadding)
            sm.Compress<true>(padding);
        else
            sm.Compress();
        const auto done = pipe.FetchEventID(HardEvent::V_S);
        SetFlag<HardEvent::V_S>(done);
        WaitFlag<HardEvent::V_S>(done);
    }

    __aicore__ inline void StoreDigests(uint32_t base, uint32_t active)
    {
        for (uint32_t chunk = 0; chunk < 8; ++chunk) {
            Gather(out32[chunk * Q], sm.C(0), outputOffsets, chunk * (Q / 8) * 4, Q);
            sm.v.Barrier();
            sm.v.ByteSwap(out32[chunk * Q], out32[chunk * Q]);
        }
        const auto ready = pipe.FetchEventID(HardEvent::V_MTE3);
        SetFlag<HardEvent::V_MTE3>(ready);
        WaitFlag<HardEvent::V_MTE3>(ready);
        DataCopy(output[base * 32], outgoing, active * 32);
        const auto done = pipe.FetchEventID(HardEvent::MTE3_V);
        SetFlag<HardEvent::MTE3_V>(done);
        WaitFlag<HardEvent::MTE3_V>(done);
    }

    __aicore__ inline void Run(uint32_t count)
    {
        uint32_t padding[68];
        const bool fixedPadding = length % 64 == 0;
        if (fixedPadding)
            ExpandPadding(length, padding);
        for (uint32_t base = GetBlockIdx() * Q; base < count; base += GetBlockNum() * Q) {
            const uint32_t active = count - base < Q ? count - base : Q;
            sm.Reset();
            for (uint32_t block = 0; block < totalBlocks; ++block)
                CompressBlock(base, block, active, fixedPadding ? padding : nullptr);
            StoreDigests(base, active);
        }
    }
};
} // namespace

__global__ __aicore__ void SM3_VECTOR_KERNEL(GM_ADDR bytes, GM_ADDR result, uint32_t count, uint32_t length)
{
    KERNEL_TASK_TYPE_DEFAULT(KERNEL_TYPE_AIV_ONLY);
    GlobalTensor<uint8_t> input, output;
    GlobalTensor<uint32_t> input32;
    input.SetGlobalBuffer(reinterpret_cast<__gm__ uint8_t*>(bytes));
    input32.SetGlobalBuffer(reinterpret_cast<__gm__ uint32_t*>(bytes));
    output.SetGlobalBuffer(reinterpret_cast<__gm__ uint8_t*>(result), count * 32);
    TPipe pipe;
    TBuf<TPosition::VECCALC> work, outBuffer;
    TBuf<TPosition::VECIN> staging;
    pipe.InitBuffer(work, 104 * Q * 4);
    pipe.InitBuffer(staging, Q * 64);
    pipe.InitBuffer(outBuffer, Q * 32);
    auto mem = work.Get<uint32_t>();
    auto incoming = staging.Get<uint32_t>();
    auto outgoing = outBuffer.Get<uint8_t>();
    Sm3 sm{mem, {mem[96 * Q], Q}};
    auto offsets = mem[102 * Q];
    for (uint32_t lane = 0; lane < Q; ++lane)
        offsets.SetValue(lane, lane * 64);
    auto outputOffsets = mem[103 * Q];
    for (uint32_t lane = 0; lane < Q; ++lane)
        outputOffsets.SetValue(lane, ((lane % 8) * Q + lane / 8) * 4);
    auto out32 = outBuffer.Get<uint32_t>();
    const auto init = pipe.FetchEventID(HardEvent::S_V);
    SetFlag<HardEvent::S_V>(init);
    WaitFlag<HardEvent::S_V>(init);
    const bool dmaAligned = (reinterpret_cast<uint64_t>(bytes) & 31) == 0 && length % 32 == 0;
    VectorBatch batch{pipe,      sm,       input,   input32,
                      output,    incoming, offsets, outputOffsets,
                      out32,     outgoing, length,  (length + 72) / 64,
                      dmaAligned};
    batch.Run(count);
}
extern "C" void SM3_VECTOR_LAUNCH(
    uint32_t blocks, void* stream, const uint8_t* input, uint8_t* output, uint32_t count, uint32_t length)
{
    const uint32_t tiles = (count + Q - 1) / Q;
    if (blocks > tiles)
        blocks = tiles;
    SM3_VECTOR_KERNEL<<<blocks, nullptr, stream>>>((GM_ADDR)input, (GM_ADDR)output, count, length);
}
