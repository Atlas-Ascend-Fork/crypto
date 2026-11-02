/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
/* Submission CPU algorithm is kept verbatim; this file only adapts the NPU
 * block partition and partial-tile staging. Compiled once for each BATCH. */
#include "aice_cpu.c"

int AICE_CPU_BATCH_CRYPT(
    uint32_t dim, int decrypt, const uint8_t* key, const uint8_t* iv, const uint8_t* input, uint8_t* output,
    uint32_t length)
{
    const uint32_t tile_bytes = (uint32_t)BLOCK_BYTES;
    const uint32_t tiles = length / tile_bytes + (length % tile_bytes != 0);
    const uint32_t quotient = tiles / dim, remainder = tiles % dim;
    ai_state_t state;
    ai_table_t table;
    uint8_t tile[BLOCK_BYTES] __attribute__((aligned(64)));
    for (uint32_t instance = 0; instance < dim && instance < tiles; ++instance) {
        const uint32_t count = quotient + (instance < remainder);
        const uint32_t start = instance * quotient + (instance < remainder ? instance : remainder);
        ai_stream_init(&state, &table, key, iv, (int)instance);
        for (uint32_t block = 0; block < count; ++block) {
            const size_t offset = (size_t)(start + block) * tile_bytes;
            const size_t bytes = length - offset < tile_bytes ? length - offset : tile_bytes;
            memset(tile, 0, sizeof(tile));
            memcpy(tile, input + offset, bytes);
            if (decrypt)
                ai_stream_decrypt(&state, &table, tile, tile, sizeof(tile));
            else
                ai_stream_encrypt(&state, &table, tile, tile, sizeof(tile));
            memcpy(output + offset, tile, bytes);
        }
    }
    return 0;
}
