/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#if SHAKE_TEST_BITS == 128
#include "../../../op_host/shake128_tensor_tiling.cpp"
using TilingData = Shake128TensorTilingData;
#else
#include "../../../op_host/shake256_tensor_tiling.cpp"
using TilingData = Shake256TensorTilingData;
#endif
#include "../../../op_host/shake_tiling.h"

#include <iostream>

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "Tiling check failed at line " << __LINE__ << '\n'; return 1; } } while (false)

int main()
{
    using cann_crypto::shake::MAX_BATCH_SIZE;
    CHECK(optiling::TilingFunc(nullptr) == ge::GRAPH_FAILED);
    for (const uint32_t batch : {1U, 16U, 33U, 64U, 207U, 208U, 209U, 257U, MAX_BATCH_SIZE}) {
        const gert::StorageShape x{batch, 0};
        const gert::StorageShape y{batch, 339};
        for (const uint32_t cores : {0U, 1U, 20U, 48U}) {
            gert::TilingContext context(&x, &y);
            context.device.coreCount = cores;
            CHECK(optiling::TilingFunc(&context) == ge::GRAPH_SUCCESS);
            const auto* data = context.GetTilingData<TilingData>();
            CHECK(data->batch == batch && data->inputLength == 0 && data->outputLength == 339);
            CHECK(data->groupSize >= 64 && data->groupSize <= 208 && data->groupSize % 16 == 0);
            CHECK(context.blockDim == data->blockDim && data->blockDim > 0);
            CHECK(context.blockDim <= (cores == 0 ? 1 : cores));
            CHECK(data->useFastPath == 0 && data->reserved0 == 0 && data->reserved1 == 0);
            CHECK(context.tilingKey == 0 && context.workspace == 0);
            CHECK(context.raw.size == sizeof(TilingData));
        }
    }
    const gert::StorageShape good{64, 32};
    for (const auto& shape : {gert::StorageShape{64}, gert::StorageShape{64, -1},
            gert::StorageShape{0, 32}, gert::StorageShape{64, 32, 1},
            gert::StorageShape{static_cast<int64_t>(MAX_BATCH_SIZE) + 1, 32}}) {
        gert::TilingContext context(&shape, &shape);
        CHECK(optiling::TilingFunc(&context) == ge::GRAPH_FAILED);
    }
    for (const auto& shape : {gert::StorageShape{63, 32}, gert::StorageShape{64, 0}}) {
        gert::TilingContext context(&good, &shape);
        CHECK(optiling::TilingFunc(&context) == ge::GRAPH_FAILED);
    }
    for (int missing = 0; missing < 5; ++missing) {
        gert::TilingContext context(&good, &good);
        if (missing == 0) { context.input = nullptr; }
        if (missing == 1) { context.output = nullptr; }
        if (missing == 2) { context.hasTiling = false; }
        if (missing == 3) { context.hasRaw = false; }
        if (missing == 4) { context.hasWorkspace = false; }
        CHECK(optiling::TilingFunc(&context) == ge::GRAPH_FAILED);
    }
    gert::TilingContext noPlatform(&good, &good);
    noPlatform.platform = nullptr;
    CHECK(optiling::TilingFunc(&noPlatform) == ge::GRAPH_SUCCESS);
    CHECK(noPlatform.blockDim == 1);
    return 0;
}
