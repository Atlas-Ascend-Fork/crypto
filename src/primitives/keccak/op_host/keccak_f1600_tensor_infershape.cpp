/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "register/op_impl_registry.h"

namespace ops {

static ge::graphStatus InferShape(gert::InferShapeContext* context)
{
    if (context == nullptr) {
        return ge::GRAPH_FAILED;
    }
    const gert::Shape* xShape = context->GetInputShape(0);
    gert::Shape* yShape = context->GetOutputShape(0);

    if (xShape == nullptr || yShape == nullptr || xShape->GetDimNum() != 2 || xShape->GetDim(0) != 50) {
        return ge::GRAPH_FAILED;
    }

    const int64_t batch = xShape->GetDim(1);
    yShape->SetDimNum(2);
    yShape->SetDim(0, 50);
    yShape->SetDim(1, batch);
    return ge::GRAPH_SUCCESS;
}

IMPL_OP_INFERSHAPE(KeccakF1600Tensor).InferShape(InferShape);

} // namespace ops
