/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "shake_tiling_common.h"
#include "../op_kernel/shake256_tensor_tiling_data.h"
#include "../op_kernel/shake256_tensor_tiling_key.h"

namespace optiling {
static ge::graphStatus TilingFunc(gert::TilingContext* context)
{
    return TilingShakeTensor<Shake256TensorTilingData>(
        context, cann_crypto::shake::Algorithm::SHAKE256, SHAKE256_TENSOR_TILING_KEY_DEFAULT);
}
IMPL_OP_OPTILING(Shake256Tensor).Tiling(TilingFunc);
} // namespace optiling
