/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "../../../op_host/keccak_f1600_tensor_tiling.cpp"

#include <iostream>

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "Tiling check failed at line " << __LINE__ << '\n'; return 1; } } while (false)

int main()
{
    CHECK(optiling::TilingFunc(nullptr) == ge::GRAPH_FAILED);
    for (const int64_t batch : {1, 63, 64, 65, 255, 256, 257, 8191, 8192}) {
        gert::StorageShape shape{50, batch};
        gert::TilingContext context(&shape, &shape);
        for (const uint32_t cores : {0U, 1U, 20U, 48U}) {
            context.device.coreCount = cores;
            CHECK(optiling::TilingFunc(&context) == ge::GRAPH_SUCCESS);
            const auto* data = context.GetTilingData<KeccakF1600TensorTilingData>();
            CHECK(data->batch == batch);
            const uint32_t tiles = static_cast<uint32_t>((batch + 255) / 256);
            const uint32_t limit = cores == 0 ? 20 : cores;
            CHECK(context.blockDim == (tiles < limit ? tiles : limit));
            CHECK(context.tilingKey == KECCAK_F1600_TENSOR_TILING_KEY_DEFAULT);
            CHECK(context.workspace == 0);
            CHECK(context.raw.size == sizeof(KeccakF1600TensorTilingData));
        }
    }
    const gert::StorageShape good{50, 64};
    for (const auto& shape : {gert::StorageShape{50}, gert::StorageShape{49, 64},
            gert::StorageShape{50, 0}, gert::StorageShape{50, -1}, gert::StorageShape{50, 8193}}) {
        gert::TilingContext context(&shape, &shape);
        CHECK(optiling::TilingFunc(&context) == ge::GRAPH_FAILED);
    }
    const gert::StorageShape wrongOutput{50, 65};
    gert::TilingContext mismatch(&good, &wrongOutput);
    CHECK(optiling::TilingFunc(&mismatch) == ge::GRAPH_FAILED);
    for (int missing = 0; missing < 6; ++missing) {
        gert::TilingContext context(&good, &good);
        if (missing == 0) { context.input = nullptr; }
        if (missing == 1) { context.output = nullptr; }
        if (missing == 2) { context.platform = nullptr; }
        if (missing == 3) { context.hasTiling = false; }
        if (missing == 4) { context.hasRaw = false; }
        if (missing == 5) { context.hasWorkspace = false; }
        CHECK(optiling::TilingFunc(&context) == ge::GRAPH_FAILED);
    }
    return 0;
}
