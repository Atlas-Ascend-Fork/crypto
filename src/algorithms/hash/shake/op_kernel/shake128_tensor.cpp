/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "shake_uniform_u32_tensor.h"

#if defined(CANN_CRYPTO_KERNEL_UT)
extern "C" __global__ __aicore__ void shake128_tensor(
    GM_ADDR x, GM_ADDR y, uint32_t batch, uint32_t inputLength, uint32_t outputLength, uint32_t groupSize)
#else
extern "C" __global__ __vector__ void shake128_tensor(
    GM_ADDR x, GM_ADDR y, uint32_t batch, uint32_t inputLength, uint32_t outputLength, uint32_t groupSize)
#endif
{
    ShakeUniformU32Tensor::KernelShakeUniformU32Tensor<168U> op;
    op.Init(x, y, batch, inputLength, outputLength, groupSize);
    op.Process();
}
