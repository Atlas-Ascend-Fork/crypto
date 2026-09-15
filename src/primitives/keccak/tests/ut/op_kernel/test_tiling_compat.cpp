/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "tiling_test_compat.h"
#include "../../../op_kernel/keccak_f1600_tensor_tiling_data.h"

#include <array>
#include <cstdint>
#include <cstring>

int main()
{
    const KeccakF1600TensorTilingData expected{257U};
    // 非对齐字节地址也应通过 memcpy 正确读取，不依赖指针强转。
    std::array<uint8_t, sizeof(expected) + 1> bytes{};
    std::memcpy(bytes.data() + 1, &expected, sizeof(expected));
    GET_TILING_DATA_WITH_STRUCT(KeccakF1600TensorTilingData, actual, bytes.data() + 1);
    if (actual.batch != expected.batch) {
        return 1;
    }
    bytes.fill(0);
    return actual.batch == expected.batch ? 0 : 1;
}
