/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "sha3_ref.h"

#include <stdio.h>
#include <string.h>

static int CheckEmpty(uint32_t algorithm, const char *expected, size_t size)
{
    uint8_t output[64];
    char actual[129];
    if (CannCryptoRefShake(algorithm, NULL, 0, output, size) != 0) {
        return 1;
    }
    for (size_t i = 0; i < size; ++i) {
        (void)snprintf(actual + i * 2, 3, "%02x", (unsigned)output[i]);
    }
    return strcmp(actual, expected) != 0;
}

int main(void)
{
    /* FIPS 202 空消息已知答案，用于固定实现的基本语义。 */
    int failed = CheckEmpty(128U,
        "7f9c2ba4e88f827d616045507605853ed73b8093f6efbc88eb1a6eacfa66ef26", 32);
    failed += CheckEmpty(256U,
        "46b9dd2b0ba88d13233b3feb743eeb243fcd52ea62b81b82b50c27646ed5762f"
        "d75dc4ddd8c0f200cb05019d67b592f6fc821c49479ab48640292eacb3b7c4be", 64);
    uint8_t output[1];
    failed += CannCryptoRefShake(1U, NULL, 0, output, 1) == 0;
    failed += CannCryptoRefShake(128U, NULL, 1, output, 1) == 0;
    failed += CannCryptoRefShake(128U, NULL, 0, NULL, 1) == 0;
    failed += CannCryptoRefShake(128U, NULL, 0, output, 0) == 0;
    puts(failed == 0 ? "REFERENCE_KAT=PASS" : "REFERENCE_KAT=FAIL");
    return failed == 0 ? 0 : 1;
}
