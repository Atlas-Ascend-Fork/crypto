/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/*!
 * \file ntt_proto.h
 * \brief
 */
#ifndef NTT_PROTO_H_
#define NTT_PROTO_H_

#include "graph/operator_reg.h"

namespace ge {
REG_OP(Ntt)
    .INPUT(x, TensorType({DT_INT32}))
    .INPUT(matrix_phase1, TensorType({DT_INT8}))
    .INPUT(matrix_phase2, TensorType({DT_INT8}))
    .INPUT(twiddle_phase12, TensorType({DT_INT32}))
    .OUTPUT(y, TensorType({DT_INT32}))
    .ATTR(block_dim, Int, 10)
    .ATTR(sub_bench, Int, 64)
    .ATTR(channel_block, Int, 1)
    .ATTR(q, Int, 8380417)
    .ATTR(g, Int, 10)
    .ATTR(logp, Int, 0)
    .ATTR(rns_channels, Int, 0)
    .ATTR(q_list, ListInt, {})
    .ATTR(g_list, ListInt, {})
    .OP_END_FACTORY_REG(Ntt)
} // namespace ge
#endif // NTT_PROTO_H_
