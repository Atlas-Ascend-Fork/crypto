/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#pragma once

#include "kernel_operator.h"
#include "../../../../primitives/keccak/op_kernel/keccak_f1600_u32_vector.h"

namespace ShakeUniformU32Tensor {

constexpr uint32_t TILE = KeccakF1600U32Vector::TILE;
constexpr uint32_t MIN_WORK_GROUP = 64U;
constexpr uint32_t MAX_WORK_GROUP = 208U;

template <uint32_t RATE_BYTES>
class KernelShakeUniformU32Tensor {
public:
    static_assert((RATE_BYTES % sizeof(uint32_t)) == 0U,
                  "SHAKE rate must contain complete U32 words");
    static_assert((RATE_BYTES / sizeof(uint32_t)) <=
                      KeccakF1600U32Vector::STATE_WORDS,
                  "SHAKE rate exceeds Keccak-f[1600] state");

    __aicore__ inline KernelShakeUniformU32Tensor() {}

    __aicore__ inline void Init(
        GM_ADDR x,
        GM_ADDR y,
        uint32_t batch,
        uint32_t inputLength,
        uint32_t outputLength,
        uint32_t groupSize)
    {
        x_ = reinterpret_cast<__gm__ uint8_t*>(x);
        y_ = reinterpret_cast<__gm__ uint8_t*>(y);
        batch_ = batch;
        inputLength_ = inputLength;
        outputLength_ = outputLength;
        groupSize_ = groupSize;
        pipe_.InitBuffer(
            buf_,
            KeccakF1600U32Vector::SLOT_COUNT * TILE * sizeof(uint32_t));
    }

    __aicore__ inline void Process()
    {
        AscendC::LocalTensor<uint32_t> base = buf_.Get<uint32_t>();

        const int32_t eventSToV = static_cast<int32_t>(
            GetTPipePtr()->FetchEventID(AscendC::HardEvent::S_V));
        const int32_t eventVToS = static_cast<int32_t>(
            GetTPipePtr()->FetchEventID(AscendC::HardEvent::V_S));

        const uint32_t blockIdx = AscendC::GetBlockIdx();
        const uint32_t blockNum = AscendC::GetBlockNum();
        const uint32_t tileCount = (batch_ + groupSize_ - 1U) / groupSize_;

        for (uint32_t tileIdx = blockIdx; tileIdx < tileCount; tileIdx += blockNum) {
            const uint32_t tileOffset = tileIdx * groupSize_;
            uint32_t validN = groupSize_;
            if (tileOffset + validN > batch_) {
                validN = batch_ - tileOffset;
            }

            uint32_t calcN = validN == groupSize_
                ? groupSize_
                : ((validN + 63U) / 64U) * 64U;
            if (calcN < MIN_WORK_GROUP) {
                calcN = MIN_WORK_GROUP;
            }
            if (calcN > TILE) {
                calcN = TILE;
            }

            ProcessTile(base, tileOffset, validN, calcN, eventSToV, eventVToS);
        }
    }

private:
    __aicore__ inline void SyncSToV(int32_t eventId)
    {
        AscendC::SetFlag<AscendC::HardEvent::S_V>(eventId);
        AscendC::WaitFlag<AscendC::HardEvent::S_V>(eventId);
    }

    __aicore__ inline void SyncVToS(int32_t eventId)
    {
        AscendC::SetFlag<AscendC::HardEvent::V_S>(eventId);
        AscendC::WaitFlag<AscendC::HardEvent::V_S>(eventId);
    }

    __aicore__ inline void ZeroState(
        const AscendC::LocalTensor<uint32_t>& base,
        uint32_t calcN)
    {
        for (uint32_t word = 0;
             word < KeccakF1600U32Vector::STATE_WORDS;
             ++word) {
            AscendC::Duplicate(
                permutation_.Slot(base, KeccakF1600U32Vector::STATE_BASE + word),
                static_cast<uint32_t>(0),
                calcN);
        }
        AscendC::PipeBarrier<PIPE_V>();
    }

    __aicore__ inline uint32_t LoadWord(
        uint32_t message,
        uint32_t byteOffset,
        uint32_t byteCount)
    {
        uint32_t value = 0U;
        const uint64_t rowBase =
            static_cast<uint64_t>(message) * static_cast<uint64_t>(inputLength_);
        for (uint32_t byte = 0; byte < byteCount; ++byte) {
            value |= static_cast<uint32_t>(x_[rowBase + byteOffset + byte])
                     << (byte * 8U);
        }
        return value;
    }

    __aicore__ inline void XorInputBlock(
        const AscendC::LocalTensor<uint32_t>& base,
        uint32_t tileOffset,
        uint32_t validN,
        uint32_t blockOffset,
        uint32_t blockBytes)
    {
        // 外部消息按行连续；在本地缓冲区重排为同一状态字跨消息的向量。
        const uint32_t wordCount = (blockBytes + 3U) / 4U;
        for (uint32_t word = 0; word < wordCount; ++word) {
            uint32_t bytesInWord = blockBytes - word * 4U;
            if (bytesInWord > 4U) {
                bytesInWord = 4U;
            }

            AscendC::LocalTensor<uint32_t> stateWord = permutation_.Slot(
                base, KeccakF1600U32Vector::STATE_BASE + word);
            for (uint32_t localMessage = 0;
                 localMessage < validN;
                 ++localMessage) {
                const uint32_t message = tileOffset + localMessage;
                const uint32_t value = LoadWord(
                    message, blockOffset + word * 4U, bytesInWord);
                stateWord.SetValue(
                    localMessage,
                    stateWord.GetValue(localMessage) ^ value);
            }
        }
    }

    __aicore__ inline void ApplyPadding(
        const AscendC::LocalTensor<uint32_t>& base,
        uint32_t validN,
        uint32_t remainder)
    {
        // SHAKE 使用 0x1f 域分隔，并在 rate 的最后一个字节置最高位。
        const uint32_t suffixWord = remainder / 4U;
        const uint32_t suffixShift = (remainder % 4U) * 8U;
        const uint32_t suffixMask = static_cast<uint32_t>(0x1fU) << suffixShift;

        const uint32_t finalByte = RATE_BYTES - 1U;
        const uint32_t finalWord = finalByte / 4U;
        const uint32_t finalShift = (finalByte % 4U) * 8U;
        const uint32_t finalMask = static_cast<uint32_t>(0x80U) << finalShift;

        AscendC::LocalTensor<uint32_t> suffix = permutation_.Slot(
            base, KeccakF1600U32Vector::STATE_BASE + suffixWord);
        AscendC::LocalTensor<uint32_t> final = permutation_.Slot(
            base, KeccakF1600U32Vector::STATE_BASE + finalWord);

        for (uint32_t localMessage = 0;
             localMessage < validN;
             ++localMessage) {
            suffix.SetValue(
                localMessage,
                suffix.GetValue(localMessage) ^ suffixMask);
            final.SetValue(
                localMessage,
                final.GetValue(localMessage) ^ finalMask);
        }
    }

    __aicore__ inline void StoreOutputChunk(
        const AscendC::LocalTensor<uint32_t>& base,
        uint32_t tileOffset,
        uint32_t validN,
        uint32_t outputOffset,
        uint32_t chunkBytes)
    {
        const uint32_t wordCount = (chunkBytes + 3U) / 4U;
        for (uint32_t word = 0; word < wordCount; ++word) {
            uint32_t bytesInWord = chunkBytes - word * 4U;
            if (bytesInWord > 4U) {
                bytesInWord = 4U;
            }

            AscendC::LocalTensor<uint32_t> stateWord = permutation_.Slot(
                base, KeccakF1600U32Vector::STATE_BASE + word);
            for (uint32_t localMessage = 0;
                 localMessage < validN;
                 ++localMessage) {
                const uint32_t value = stateWord.GetValue(localMessage);
                const uint32_t message = tileOffset + localMessage;
                const uint64_t rowBase =
                    static_cast<uint64_t>(message) *
                    static_cast<uint64_t>(outputLength_);
                for (uint32_t byte = 0; byte < bytesInWord; ++byte) {
                    y_[rowBase + outputOffset + word * 4U + byte] =
                        static_cast<uint8_t>((value >> (byte * 8U)) & 0xffU);
                }
            }
        }
    }

    __aicore__ inline void ProcessTile(
        const AscendC::LocalTensor<uint32_t>& base,
        uint32_t tileOffset,
        uint32_t validN,
        uint32_t calcN,
        int32_t eventSToV,
        int32_t eventVToS)
    {
        ZeroState(base, calcN);
        SyncVToS(eventVToS);

        uint32_t inputOffset = 0U;
        // 恰好整块的消息也先吸收，再处理一个单独的填充块。
        while (inputLength_ - inputOffset >= RATE_BYTES) {
            XorInputBlock(
                base, tileOffset, validN, inputOffset, RATE_BYTES);
            SyncSToV(eventSToV);
            permutation_.Permute24(base, calcN);
            SyncVToS(eventVToS);
            inputOffset += RATE_BYTES;
        }

        const uint32_t remainder = inputLength_ - inputOffset;
        XorInputBlock(base, tileOffset, validN, inputOffset, remainder);
        ApplyPadding(base, validN, remainder);
        SyncSToV(eventSToV);
        permutation_.Permute24(base, calcN);
        SyncVToS(eventVToS);

        uint32_t produced = 0U;
        // 每次最多输出一个 rate，仍需更多字节时再次执行置换。
        while (produced < outputLength_) {
            uint32_t chunkBytes = outputLength_ - produced;
            if (chunkBytes > RATE_BYTES) {
                chunkBytes = RATE_BYTES;
            }
            StoreOutputChunk(
                base, tileOffset, validN, produced, chunkBytes);
            produced += chunkBytes;
            if (produced < outputLength_) {
                SyncSToV(eventSToV);
                permutation_.Permute24(base, calcN);
                SyncVToS(eventVToS);
            }
        }

        // 标量读取完成后，下一组消息才能复用同一块本地状态。
        SyncSToV(eventSToV);
    }

    AscendC::TPipe pipe_;
    AscendC::TBuf<AscendC::QuePosition::VECCALC> buf_;
    __gm__ uint8_t* x_ = nullptr;
    __gm__ uint8_t* y_ = nullptr;
    KeccakF1600U32Vector::Permutation permutation_;
    uint32_t batch_ = 0U;
    uint32_t inputLength_ = 0U;
    uint32_t outputLength_ = 0U;
    uint32_t groupSize_ = MIN_WORK_GROUP;
};

}  // namespace ShakeUniformU32Tensor
