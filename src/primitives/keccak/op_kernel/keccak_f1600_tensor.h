/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_CRYPTO_KECCAK_F1600_TENSOR_H
#define CANN_CRYPTO_KECCAK_F1600_TENSOR_H

#include "kernel_operator.h"
#include "keccak_f1600_tensor_tiling_data.h"
#include "keccak_f1600_u32_vector.h"

namespace KeccakF1600TensorKernel {

using KeccakF1600U32Vector::Permutation;

class KernelKeccakF1600Tensor {
public:
    __aicore__ inline KernelKeccakF1600Tensor() {}

    __aicore__ inline void Init(GM_ADDR x, GM_ADDR y, const KeccakF1600TensorTilingData* tiling)
    {
        x_.SetGlobalBuffer(reinterpret_cast<__gm__ uint32_t*>(x));
        y_.SetGlobalBuffer(reinterpret_cast<__gm__ uint32_t*>(y));
        batch_ = tiling->batch;
        pipe_.InitBuffer(buf_, KeccakF1600U32Vector::SLOT_COUNT * KeccakF1600U32Vector::TILE * sizeof(uint32_t));
    }

    __aicore__ inline void Process()
    {
        AscendC::LocalTensor<uint32_t> base = buf_.Get<uint32_t>();

        const int32_t eventVToMte2 = static_cast<int32_t>(GetTPipePtr()->FetchEventID(AscendC::HardEvent::V_MTE2));
        const int32_t eventMte2ToV = static_cast<int32_t>(GetTPipePtr()->FetchEventID(AscendC::HardEvent::MTE2_V));
        const int32_t eventVToMte3 = static_cast<int32_t>(GetTPipePtr()->FetchEventID(AscendC::HardEvent::V_MTE3));
        const int32_t eventMte3ToV = static_cast<int32_t>(GetTPipePtr()->FetchEventID(AscendC::HardEvent::MTE3_V));
        const int32_t eventMte3ToMte2 =
            static_cast<int32_t>(GetTPipePtr()->FetchEventID(AscendC::HardEvent::MTE3_MTE2));

        const uint32_t blockIdx = AscendC::GetBlockIdx();
        const uint32_t blockNum = AscendC::GetBlockNum();
        const uint32_t tileCount = (batch_ + KeccakF1600U32Vector::TILE - 1U) / KeccakF1600U32Vector::TILE;

        for (uint32_t tileIdx = blockIdx; tileIdx < tileCount; tileIdx += blockNum) {
            const uint32_t offset = tileIdx * KeccakF1600U32Vector::TILE;
            uint32_t validN = KeccakF1600U32Vector::TILE;
            if (offset + validN > batch_) {
                validN = batch_ - offset;
            }

            uint32_t calcN = ((validN + 63U) / 64U) * 64U;
            if (calcN < 64U) {
                calcN = 64U;
            }
            if (calcN > KeccakF1600U32Vector::TILE) {
                calcN = KeccakF1600U32Vector::TILE;
            }

            for (uint32_t w = 0; w < KeccakF1600U32Vector::STATE_WORDS; ++w) {
                AscendC::Duplicate(
                    permutation_.Slot(base, KeccakF1600U32Vector::STATE_BASE + w), static_cast<uint32_t>(0), calcN);
            }
            AscendC::PipeBarrier<PIPE_V>();
            AscendC::SetFlag<AscendC::HardEvent::V_MTE2>(eventVToMte2);
            AscendC::WaitFlag<AscendC::HardEvent::V_MTE2>(eventVToMte2);

            for (uint32_t w = 0; w < KeccakF1600U32Vector::STATE_WORDS; ++w) {
                AscendC::DataCopy(
                    permutation_.Slot(base, KeccakF1600U32Vector::STATE_BASE + w), x_[w * batch_ + offset], validN);
            }
            AscendC::PipeBarrier<PIPE_ALL>();
            AscendC::SetFlag<AscendC::HardEvent::MTE2_V>(eventMte2ToV);
            AscendC::WaitFlag<AscendC::HardEvent::MTE2_V>(eventMte2ToV);

            permutation_.Permute24(base, calcN);

            AscendC::PipeBarrier<PIPE_ALL>();
            AscendC::SetFlag<AscendC::HardEvent::V_MTE3>(eventVToMte3);
            AscendC::WaitFlag<AscendC::HardEvent::V_MTE3>(eventVToMte3);

            for (uint32_t w = 0; w < KeccakF1600U32Vector::STATE_WORDS; ++w) {
                AscendC::DataCopy(
                    y_[w * batch_ + offset], permutation_.Slot(base, KeccakF1600U32Vector::STATE_BASE + w), validN);
            }
            AscendC::PipeBarrier<PIPE_ALL>();
            AscendC::SetFlag<AscendC::HardEvent::MTE3_V>(eventMte3ToV);
            AscendC::WaitFlag<AscendC::HardEvent::MTE3_V>(eventMte3ToV);
            AscendC::SetFlag<AscendC::HardEvent::MTE3_MTE2>(eventMte3ToMte2);
            AscendC::WaitFlag<AscendC::HardEvent::MTE3_MTE2>(eventMte3ToMte2);
        }
    }

private:
    AscendC::TPipe pipe_;
    AscendC::TBuf<AscendC::QuePosition::VECCALC> buf_;
    AscendC::GlobalTensor<uint32_t> x_;
    AscendC::GlobalTensor<uint32_t> y_;
    Permutation permutation_;
    uint32_t batch_ = 1;
};

} // namespace KeccakF1600TensorKernel

#endif
