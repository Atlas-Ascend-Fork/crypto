/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/*!
 * \file ntt_infershape.cpp
 * \brief
 */
#include "register/op_impl_registry.h"

namespace ops {

static ge::graphStatus InferShapeNtt(gert::InferShapeContext* context)
{
    if (context == nullptr) {
        return ge::GRAPH_FAILED;
    }

    const gert::Shape* xShape = context->GetInputShape(0);
    if (xShape == nullptr) {
        return ge::GRAPH_FAILED;
    }

    gert::Shape* yShape = context->GetOutputShape(0);
    if (yShape == nullptr) {
        return ge::GRAPH_FAILED;
    }

    auto xShapeSize = xShape->GetDimNum();
    yShape->SetDimNum(xShapeSize);
    for (size_t i = 0; i < xShapeSize; i++) {
        int64_t dim = xShape->GetDim(i);
        yShape->SetDim(i, dim);
    }

    return ge::GRAPH_SUCCESS;
}

IMPL_OP_INFERSHAPE(Ntt).InferShape(InferShapeNtt);
} // namespace ops
