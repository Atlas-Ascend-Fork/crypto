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

// helper macros
#define S(i, batchSize) ((i) * (batchSize))
#define SIZE(i, batchSize) ((i) * (batchSize))
#define BYTES(i, batchSize) ((i) * (batchSize) * sizeof(uint16_t))

namespace cann_crypto::algorithms::cipher::aice {

constexpr uint16_t rotateConsts[37] = {11, 15, 15, 7,  12, 2, 13, 1, 6, 8,  1,  5, 14, 12, 10, 9,  5,  15, 4,
                                       7,  12, 11, 15, 13, 7, 4,  8, 1, 11, 14, 9, 8,  3,  5,  14, 10, 10};

namespace {

template <uint32_t BatchSize>
class AiceDevice {
    static_assert(BatchSize == 128 || BatchSize == 256 || BatchSize == 512);

public:
    __aicore__ inline AiceDevice() {}

    __aicore__ inline void Init(
        GM_ADDR input, GM_ADDR key, GM_ADDR iv, GM_ADDR output, size_t inputLength, uint32_t instanceIndex)
    {
        instanceIndex_ = instanceIndex;
        inputGm_.SetGlobalBuffer((__gm__ uint8_t*)input, inputLength);
        outputGm_.SetGlobalBuffer((__gm__ uint8_t*)output, inputLength);
        keyGm_.SetGlobalBuffer((__gm__ uint16_t*)key, 16);
        ivGm_.SetGlobalBuffer((__gm__ uint16_t*)iv, 6);

        pipe_.InitBuffer(inputQueue_, 1, BYTES(32, BatchSize));
        pipe_.InitBuffer(outputQueue_, 1, BYTES(32, BatchSize));
        pipe_.InitBuffer(keyQueue_, 1, 16 * sizeof(uint16_t));
        pipe_.InitBuffer(ivQueue_, 1, 16 * sizeof(uint16_t));
#if defined(__NPU_ARCH__) && __NPU_ARCH__ == 3510
        pipe_.InitBuffer(tmpBuffer_, BYTES(22, BatchSize));
        // Two 37-row tables: per-lane left/right shift counts.
        pipe_.InitBuffer(calcBuffer_, BYTES(74, BatchSize));
#else
        pipe_.InitBuffer(tmpBuffer_, BYTES(16, BatchSize));
        // One 37-row table: powers of two used to emulate left rotation with multiplication.
        pipe_.InitBuffer(calcBuffer_, BYTES(37, BatchSize));
#endif
        // SoA S[0..36] plus a 37-row update window, reused for 32-row output memory.
        pipe_.InitBuffer(stateBuffer_, BYTES(74, BatchSize));
        states_ = stateBuffer_.Get<uint16_t>();

        // Rotation tables are independent of key/IV and prepared once per Init.
        AscendC::LocalTensor<uint16_t> calc = calcBuffer_.Get<uint16_t>();
#if defined(__NPU_ARCH__) && __NPU_ARCH__ == 3510
        for (uint32_t i = 0U; i < 37; i++) {
            AscendC::Duplicate(calc[S(i, BatchSize)], static_cast<uint16_t>(rotateConsts[i]), SIZE(1, BatchSize));
            AscendC::Duplicate(
                calc[S(37 + i, BatchSize)], static_cast<uint16_t>(16 - rotateConsts[i]), SIZE(1, BatchSize));
        }
#else
        for (uint32_t i = 0U; i < 37; i++) {
            const uint16_t value = static_cast<uint16_t>(1U << rotateConsts[i]);
            AscendC::Duplicate(calc[S(i, BatchSize)], (uint16_t)value, SIZE(1, BatchSize));
        }
#endif
        initialized_ = false;
    }

    __aicore__ inline void Encrypt(size_t inputOffset, size_t inputLength) { Process<true>(inputOffset, inputLength); }

    __aicore__ inline void Decrypt(size_t inputOffset, size_t inputLength) { Process<false>(inputOffset, inputLength); }

    __aicore__ inline void Destroy() { pipe_.Destroy(); }

private:
    template <bool Encrypting>
    __aicore__ inline void Process(size_t inputOffset, size_t inputLength)
    {
        if (inputLength == 0U) {
            return;
        }
        InitializeStateIfNeeded();
        constexpr size_t tileBytes = 64U * BatchSize;
        for (size_t done = 0; done < inputLength;) {
            const size_t remaining = inputLength - done;
            const size_t bytes = remaining < tileBytes ? remaining : tileBytes;
            const size_t offset = inputOffset + done;
            const size_t words = (bytes + sizeof(uint16_t) - 1U) / sizeof(uint16_t);
            CopyIn(offset, bytes);
            if constexpr (Encrypting) {
                DoEncrypt(words);
            } else {
                DoDecrypt(words);
            }
            CopyOut(offset, bytes);
            done += bytes;
        }
    }

    __aicore__ inline void InitializeStateIfNeeded()
    {
        if (initialized_) {
            return;
        }

        // 1. Stage the 32B key and 12B IV from GM into their UB queues.
        auto key = keyQueue_.AllocTensor<uint16_t>();
        auto iv = ivQueue_.AllocTensor<uint16_t>();
        AscendC::DataCopy(key, keyGm_, 16);
        AscendC::DataCopyExtParams ivCopy{1, 12, 0, 0, 0};
        AscendC::DataCopyPadExtParams<uint16_t> ivPad{true, 0, 10, 0};
        AscendC::DataCopyPad(iv, ivGm_, ivCopy, ivPad);
        keyQueue_.EnQue(key);
        ivQueue_.EnQue(iv);

        // 2. Acquire the loaded values and perform all AICE initialization phases.
        key = keyQueue_.DeQue<uint16_t>();
        iv = ivQueue_.DeQue<uint16_t>();
        Initialization(states_, key, iv);

        // 3. Release staging buffers and keep the initialized state for subsequent segments.
        keyQueue_.FreeTensor(key);
        ivQueue_.FreeTensor(iv);
        initialized_ = true;
    }

    __aicore__ inline void Initialization(
        AscendC::LocalTensor<uint16_t>& states, AscendC::LocalTensor<uint16_t>& key, AscendC::LocalTensor<uint16_t>& iv)
    {
        AscendC::LocalTensor<int16_t> i16states = states.ReinterpretCast<int16_t>();
        AscendC::LocalTensor<int16_t> i16keys = key.ReinterpretCast<int16_t>();

        // 1. Seed S[0..7] with IV96 || LE32(lane), S[8..23] with key, and S[24..36] with constants.
        InitializeIv(states, iv);
        // load and broadcast key
        for (uint32_t i = 0U; i < 16; i++) {
            AscendC::Duplicate(states[S(i + 8, BatchSize)], key.GetValue(i), SIZE(1, BatchSize));
        }
        // load constants
        AscendC::Duplicate(states[S(24, BatchSize)], (uint16_t)0x243F, SIZE(1, BatchSize));
        AscendC::Duplicate(states[S(25, BatchSize)], (uint16_t)0x6A88, SIZE(1, BatchSize));
        AscendC::Duplicate(states[S(26, BatchSize)], (uint16_t)0x85A3, SIZE(1, BatchSize));
        AscendC::Duplicate(states[S(27, BatchSize)], (uint16_t)0x08D3, SIZE(1, BatchSize));
        AscendC::Duplicate(states[S(28, BatchSize)], (uint16_t)0x1319, SIZE(1, BatchSize));
        AscendC::Duplicate(states[S(29, BatchSize)], (uint16_t)0x8A2E, SIZE(1, BatchSize));
        AscendC::Duplicate(states[S(30, BatchSize)], (uint16_t)0x0370, SIZE(1, BatchSize));
        AscendC::Duplicate(states[S(31, BatchSize)], (uint16_t)0x7344, SIZE(1, BatchSize));
        AscendC::Duplicate(states[S(32, BatchSize)], (uint16_t)0xA409, SIZE(1, BatchSize));
        AscendC::Duplicate(states[S(33, BatchSize)], (uint16_t)0x3822, SIZE(1, BatchSize));
        AscendC::Duplicate(states[S(34, BatchSize)], (uint16_t)0x299F, SIZE(1, BatchSize));
        AscendC::Duplicate(states[S(35, BatchSize)], (uint16_t)0x31D0, SIZE(1, BatchSize));
        AscendC::Duplicate(states[S(36, BatchSize)], (uint16_t)0x082E, SIZE(1, BatchSize));

        // 2. Diffusion phase: 9 * 37 = 333 state-update rounds, without producing output memory.
        for (uint32_t i = 0U; i < 9; i++) {
            BatchStateUpdate(states, 11, 0);
            BatchStateUpdate(states[S(11, BatchSize)], 11, 11);
            BatchStateUpdate(states[S(22, BatchSize)], 11, 22);
            BatchStateUpdate(states[S(33, BatchSize)], 4, 33);
            AscendC::DataCopy(states, states[S(37, BatchSize)], SIZE(37, BatchSize));
        }

        // 3. Key feed-forward: add the repeated 16-word key into all 37 state words.
        for (uint32_t i = 0U; i < 16; i++) {
            AscendC::Adds(
                i16states[S(i, BatchSize)], i16states[S(i, BatchSize)], i16keys.GetValue(i), SIZE(1, BatchSize));
            AscendC::Adds(
                i16states[S(i + 16, BatchSize)], i16states[S(i + 16, BatchSize)], i16keys.GetValue(i),
                SIZE(1, BatchSize));
        }
        for (uint32_t i = 0U; i < 5; i++) {
            AscendC::Adds(
                i16states[S(i + 32, BatchSize)], i16states[S(i + 32, BatchSize)], i16keys.GetValue(i),
                SIZE(1, BatchSize));
        }

        // 4. Output memory initialization: advance 37 rounds and cache 32 output words per lane.
        // The scratch tile stays in UB; initialization writes no ciphertext to GM.
        auto scratch = outputQueue_.AllocTensor<uint16_t>();
        Keystream(states, scratch);
        outputQueue_.FreeTensor(scratch);
    }

    __aicore__ inline void DoEncrypt(size_t tileWords)
    {
        AscendC::LocalTensor<uint16_t> inLocal = inputQueue_.DeQue<uint16_t>();
        AscendC::LocalTensor<uint16_t> outLocal = outputQueue_.AllocTensor<uint16_t>();
        AscendC::LocalTensor<int16_t> i16states = states_.ReinterpretCast<int16_t>();

        AscendC::Add(
            inLocal.ReinterpretCast<int16_t>(), inLocal.ReinterpretCast<int16_t>(), i16states[S(37, BatchSize)],
            tileWords);
        Keystream(states_, outLocal);
        AscendC::Add(
            outLocal.ReinterpretCast<int16_t>(), inLocal.ReinterpretCast<int16_t>(),
            outLocal.ReinterpretCast<int16_t>(), tileWords);
        outputQueue_.EnQue<uint16_t>(outLocal);
        inputQueue_.FreeTensor(inLocal);
    }

    __aicore__ inline void DoDecrypt(size_t tileWords)
    {
        AscendC::LocalTensor<uint16_t> inLocal = inputQueue_.DeQue<uint16_t>();
        AscendC::LocalTensor<uint16_t> outLocal = outputQueue_.AllocTensor<uint16_t>();
        AscendC::LocalTensor<int16_t> i16states = states_.ReinterpretCast<int16_t>();
        AscendC::Sub(
            inLocal.ReinterpretCast<int16_t>(), inLocal.ReinterpretCast<int16_t>(), i16states[S(37, BatchSize)],
            tileWords);
        Keystream(states_, outLocal);
        AscendC::Sub(
            outLocal.ReinterpretCast<int16_t>(), inLocal.ReinterpretCast<int16_t>(),
            outLocal.ReinterpretCast<int16_t>(), tileWords);
        outputQueue_.EnQue<uint16_t>(outLocal);
        inputQueue_.FreeTensor(inLocal);
    }

    __aicore__ inline void Keystream(AscendC::LocalTensor<uint16_t>& states, AscendC::LocalTensor<uint16_t>& c)
    {
        AscendC::LocalTensor<int16_t> i16states = states.ReinterpretCast<int16_t>();
        AscendC::LocalTensor<int16_t> i16c = c.ReinterpretCast<int16_t>();

        // Advance one 37-round period and derive 32 output words per lane.
        BatchStateUpdate(states, 11, 0);
        BatchStateUpdate(states[S(11, BatchSize)], 11, 11);
        BatchStateUpdate(states[S(22, BatchSize)], 11, 22);
        BatchStateUpdate(states[S(33, BatchSize)], 4, 33);
        AscendC::Add(i16c, i16states[S(1, BatchSize)], i16states[S(5, BatchSize)], SIZE(32, BatchSize));
        AscendC::Add(i16c, i16c, i16states[S(19, BatchSize)], SIZE(32, BatchSize));
        AscendC::Add(i16c, i16c, i16states[S(32, BatchSize)], SIZE(32, BatchSize));
#if defined(__NPU_ARCH__) && __NPU_ARCH__ == 3510
        AscendC::Xor(c, c, states[S(30, BatchSize)], SIZE(32, BatchSize));
#else
        auto cache = tmpBuffer_.Get<uint16_t>();
        auto i16cache = cache.ReinterpretCast<int16_t>();
        AscendC::Or(cache, c, states[S(30, BatchSize)], SIZE(16, BatchSize));
        AscendC::And(c, c, states[S(30, BatchSize)], SIZE(16, BatchSize));
        AscendC::Sub(i16c, i16cache, i16c, SIZE(16, BatchSize));
        AscendC::Or(cache, c[S(16, BatchSize)], states[S(46, BatchSize)], SIZE(16, BatchSize));
        AscendC::And(c[S(16, BatchSize)], c[S(16, BatchSize)], states[S(46, BatchSize)], SIZE(16, BatchSize));
        AscendC::Sub(i16c[S(16, BatchSize)], i16cache, i16c[S(16, BatchSize)], SIZE(16, BatchSize));
#endif

        AscendC::DataCopy(states, states[S(37, BatchSize)], SIZE(37, BatchSize));
        AscendC::DataCopy(states[S(37, BatchSize)], c, SIZE(32, BatchSize));
    }

    __aicore__ inline void BatchStateUpdate(const AscendC::LocalTensor<uint16_t>& states, uint32_t batch, uint32_t pos)
    {
        AscendC::LocalTensor<int16_t> i16states = states.ReinterpretCast<int16_t>();
        AscendC::LocalTensor<uint16_t> cache = tmpBuffer_.Get<uint16_t>();
        AscendC::LocalTensor<int16_t> i16cache = cache.ReinterpretCast<int16_t>();
        AscendC::LocalTensor<int16_t> i16calc = calcBuffer_.Get<int16_t>();

        // t0 = S0 + (S15 or 1) * S25
        AscendC::Duplicate(cache, (uint16_t)1, SIZE(batch, BatchSize));
        AscendC::Or(cache, cache, states[S(15, BatchSize)], SIZE(batch, BatchSize));
        AscendC::Mul(i16states[S(37, BatchSize)], i16states[S(25, BatchSize)], i16cache, SIZE(batch, BatchSize));
        AscendC::Add(
            i16states[S(37, BatchSize)], i16states[S(37, BatchSize)], i16states[S(0, BatchSize)],
            SIZE(batch, BatchSize));

        // t1 = t0 <<< RC[r mod 37]
#if defined(__NPU_ARCH__) && __NPU_ARCH__ == 3510
        AscendC::ShiftLeft(cache, states[S(37, BatchSize)], i16calc[S(pos, BatchSize)], SIZE(batch, BatchSize));
        AscendC::ShiftRight(
            states[S(37, BatchSize)], states[S(37, BatchSize)], i16calc[S(37 + pos, BatchSize)],
            SIZE(batch, BatchSize));
        AscendC::Or(states[S(37, BatchSize)], states[S(37, BatchSize)], cache, SIZE(batch, BatchSize));
#else
        AscendC::Mul(i16cache, i16states[S(37, BatchSize)], i16calc[S(pos, BatchSize)], SIZE(batch, BatchSize));
        for (uint32_t i = 0U; i < batch; i += 1) {
            AscendC::ShiftRight(
                states[S(37 + i, BatchSize)], states[S(37 + i, BatchSize)], (uint16_t)(16 - rotateConsts[pos + i]),
                SIZE(1, BatchSize));
        }
        AscendC::Or(i16states[S(37, BatchSize)], i16states[S(37, BatchSize)], i16cache, SIZE(batch, BatchSize));
#endif

        // t2 = t1 xor S22
#if defined(__NPU_ARCH__) && __NPU_ARCH__ == 3510
        AscendC::Xor(
            states[S(37, BatchSize)], states[S(37, BatchSize)], states[S(22, BatchSize)], SIZE(batch, BatchSize));
#else
        AscendC::Or(cache, states[S(37, BatchSize)], states[S(22, BatchSize)], SIZE(batch, BatchSize));
        AscendC::And(
            states[S(37, BatchSize)], states[S(37, BatchSize)], states[S(22, BatchSize)], SIZE(batch, BatchSize));
        AscendC::Sub(i16states[S(37, BatchSize)], i16cache, i16states[S(37, BatchSize)], SIZE(batch, BatchSize));
#endif

        // next = t2 + S17
        AscendC::Add(
            i16states[S(37, BatchSize)], i16states[S(17, BatchSize)], i16states[S(37, BatchSize)],
            SIZE(batch, BatchSize));
    }

    __aicore__ inline void InitializeIv(AscendC::LocalTensor<uint16_t>& states, AscendC::LocalTensor<uint16_t>& iv)
    {
        // Initial 128-bit value: external IV96 followed by LE32(global lane).
        for (uint32_t word = 0U; word < 6; ++word) {
            AscendC::Duplicate(states[S(word, BatchSize)], iv.GetValue(word), SIZE(1, BatchSize));
        }
        const uint32_t firstLane = instanceIndex_ * BatchSize;
        auto low = states[S(6, BatchSize)].ReinterpretCast<int16_t>();
        const int16_t firstLow = static_cast<int16_t>(firstLane & 0xFFFFU);
        AscendC::CreateVecIndex(low, firstLow, BatchSize);
        AscendC::Duplicate(states[S(7, BatchSize)], static_cast<uint16_t>(firstLane >> 16U), BatchSize);
    }

    __aicore__ inline void CopyIn(size_t byteOffset, size_t byteLength)
    {
        auto input = inputQueue_.AllocTensor<uint16_t>();
        auto inputBytes = input.ReinterpretCast<uint8_t>();
        if (byteLength % 32U == 0U) {
            AscendC::DataCopy(inputBytes, inputGm_[byteOffset], static_cast<uint32_t>(byteLength));
        } else {
            AscendC::DataCopyExtParams copyParams{1, static_cast<uint32_t>(byteLength), 0, 0, 0};
            AscendC::DataCopyPadExtParams<uint8_t> padParams{true, 0, static_cast<uint8_t>(32U - byteLength % 32U), 0};
            AscendC::DataCopyPad(inputBytes, inputGm_[byteOffset], copyParams, padParams);
        }
        inputQueue_.EnQue(input);
    }

    __aicore__ inline void CopyOut(size_t byteOffset, size_t byteLength)
    {
        auto output = outputQueue_.DeQue<uint16_t>();
        auto outputBytes = output.ReinterpretCast<uint8_t>();
        if (byteLength % 32U == 0U) {
            AscendC::DataCopy(outputGm_[byteOffset], outputBytes, static_cast<uint32_t>(byteLength));
        } else {
            AscendC::DataCopyExtParams copyParams{1, static_cast<uint32_t>(byteLength), 0, 0, 0};
            AscendC::DataCopyPad(outputGm_[byteOffset], outputBytes, copyParams);
        }
        outputQueue_.FreeTensor(output);
    }

    AscendC::TPipe pipe_;
    AscendC::GlobalTensor<uint8_t> inputGm_;
    AscendC::GlobalTensor<uint8_t> outputGm_;
    AscendC::GlobalTensor<uint16_t> keyGm_;
    AscendC::GlobalTensor<uint16_t> ivGm_;
    AscendC::TQue<AscendC::QuePosition::VECIN, 1> inputQueue_;
    AscendC::TQue<AscendC::QuePosition::VECIN, 1> keyQueue_;
    AscendC::TQue<AscendC::QuePosition::VECIN, 1> ivQueue_;
    AscendC::TQue<AscendC::QuePosition::VECOUT, 1> outputQueue_;
    AscendC::TBuf<AscendC::TPosition::VECCALC> tmpBuffer_;
    AscendC::TBuf<AscendC::TPosition::VECCALC> calcBuffer_;
    AscendC::TBuf<AscendC::TPosition::VECCALC> stateBuffer_;
    AscendC::LocalTensor<uint16_t> states_;
    bool initialized_{false};
    uint32_t instanceIndex_{0U};
};

} // namespace

} // namespace cann_crypto::algorithms::cipher::aice

#undef BYTES
#undef SIZE
#undef S
