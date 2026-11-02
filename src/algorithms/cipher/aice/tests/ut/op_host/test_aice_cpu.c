/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "aice_cpu_batch.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                                  \
    do {                                                                                  \
        if (!(condition)) {                                                               \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
            return EXIT_FAILURE;                                                          \
        }                                                                                 \
    } while (0)

static int test_iv12_regression_vector(void)
{
    static const uint8_t key[32] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
                                    0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
                                    0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};
    static const uint8_t iv[12] = {0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xab};
    static const uint8_t plain[64] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c,
                                      0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
                                      0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26,
                                      0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33,
                                      0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f};
    /* Frozen with an independent scalar list-shift model of IV96 || LE32(lane). */
    static const uint8_t expected[64] = {0x86, 0x85, 0x40, 0xc0, 0x20, 0xae, 0x34, 0x00, 0x07, 0x69, 0x74, 0xff, 0x11,
                                         0x92, 0x70, 0xe6, 0x52, 0xeb, 0x80, 0xdb, 0xca, 0x02, 0x6b, 0x8b, 0xc9, 0x00,
                                         0xac, 0x28, 0xc3, 0xfa, 0x15, 0x85, 0x18, 0x3f, 0x62, 0x0a, 0x8d, 0xd2, 0xf1,
                                         0x08, 0x0c, 0x67, 0x6c, 0x4a, 0x71, 0x32, 0x67, 0xc7, 0x20, 0x0a, 0xca, 0xea,
                                         0x21, 0x17, 0x85, 0x30, 0x9d, 0x8e, 0xaf, 0xf5, 0x6b, 0x28, 0x2c, 0x3d};
    uint8_t output[sizeof(plain)] = {0};

    CHECK(aice_cpu_batch_crypt(128, 1, AICE_CPU_ENCRYPT, key, iv, plain, output, sizeof(plain)) == AICE_CPU_SUCCESS);
    CHECK(memcmp(output, expected, sizeof(output)) == 0);
    return EXIT_SUCCESS;
}

static int test_iv12_lane_index_high_word(void)
{
    const uint32_t tile_bytes = 64U * 128U;
    const uint32_t length = 512U * tile_bytes + 2U;
    uint8_t key[32];
    uint8_t iv[12];
    uint8_t* plain = (uint8_t*)calloc(length, 1);
    uint8_t* cipher = (uint8_t*)malloc(length);
    CHECK(plain != NULL && cipher != NULL);
    for (uint32_t i = 0; i < sizeof(key); ++i) {
        key[i] = (uint8_t)i;
    }
    for (uint32_t i = 0; i < sizeof(iv); ++i) {
        iv[i] = (uint8_t)(0xa0U + i);
    }
    // 513 instances each receive one tile. The last instance starts at
    // global lane 512*128 = 65536, encoded as 00 00 01 00 after the 12B IV.
    CHECK(aice_cpu_batch_crypt(128, 513, AICE_CPU_ENCRYPT, key, iv, plain, cipher, length) == AICE_CPU_SUCCESS);
    CHECK(cipher[length - 2U] == 0x2d && cipher[length - 1U] == 0x31);
    free(cipher);
    free(plain);
    return EXIT_SUCCESS;
}

static uint32_t next_random(uint32_t* state)
{
    *state = *state * 1664525U + 1013904223U;
    return *state;
}

static int test_roundtrip_and_in_place(void)
{
    const uint32_t batches[] = {128, 256, 512};
    const uint32_t lengths[] = {0, 1, 15, 16, 17, 8191, 8192, 8193, 32769};
    uint8_t key[32];
    uint8_t iv[12];
    uint32_t random_state = 0x5a17c3e9U;

    for (size_t batch_index = 0; batch_index < 3; ++batch_index) {
        for (size_t length_index = 0; length_index < 9; ++length_index) {
            const uint32_t length = lengths[length_index];
            const size_t allocation = length == 0 ? 1U : length;
            uint8_t* plain = (uint8_t*)malloc(allocation);
            uint8_t* cipher = (uint8_t*)malloc(allocation);
            uint8_t* roundtrip = (uint8_t*)malloc(allocation);
            CHECK(plain != NULL && cipher != NULL && roundtrip != NULL);
            for (size_t i = 0; i < sizeof(key); ++i) {
                key[i] = (uint8_t)next_random(&random_state);
            }
            for (size_t i = 0; i < sizeof(iv); ++i) {
                iv[i] = (uint8_t)next_random(&random_state);
            }
            for (uint32_t i = 0; i < length; ++i) {
                plain[i] = (uint8_t)next_random(&random_state);
            }

            CHECK(
                aice_cpu_batch_crypt(batches[batch_index], 5, AICE_CPU_ENCRYPT, key, iv, plain, cipher, length) ==
                AICE_CPU_SUCCESS);
            CHECK(
                aice_cpu_batch_crypt(batches[batch_index], 5, AICE_CPU_DECRYPT, key, iv, cipher, roundtrip, length) ==
                AICE_CPU_SUCCESS);
            CHECK(memcmp(plain, roundtrip, length) == 0);

            memcpy(roundtrip, plain, length);
            CHECK(
                aice_cpu_batch_crypt(
                    batches[batch_index], 5, AICE_CPU_ENCRYPT, key, iv, roundtrip, roundtrip, length) ==
                AICE_CPU_SUCCESS);
            CHECK(memcmp(roundtrip, cipher, length) == 0);
            CHECK(
                aice_cpu_batch_crypt(
                    batches[batch_index], 5, AICE_CPU_DECRYPT, key, iv, roundtrip, roundtrip, length) ==
                AICE_CPU_SUCCESS);
            CHECK(memcmp(roundtrip, plain, length) == 0);
            free(roundtrip);
            free(cipher);
            free(plain);
        }
    }
    return EXIT_SUCCESS;
}

static int test_invalid_arguments(void)
{
    uint8_t key[32] = {0};
    uint8_t iv[12] = {0};
    uint8_t byte = 0;
    CHECK(aice_cpu_batch_crypt(128, 0, AICE_CPU_ENCRYPT, key, iv, &byte, &byte, 1) == AICE_CPU_INVALID_ARGUMENT);
    CHECK(aice_cpu_batch_crypt(0, 1, AICE_CPU_ENCRYPT, key, iv, &byte, &byte, 1) == AICE_CPU_INVALID_ARGUMENT);
    CHECK(aice_cpu_batch_crypt(32, 1, AICE_CPU_ENCRYPT, key, iv, &byte, &byte, 1) == AICE_CPU_INVALID_ARGUMENT);
    CHECK(aice_cpu_batch_crypt(129, 1, AICE_CPU_ENCRYPT, key, iv, &byte, &byte, 1) == AICE_CPU_INVALID_ARGUMENT);
    CHECK(
        aice_cpu_batch_crypt(128, 1, (aice_cpu_batch_operation_t)99, key, iv, &byte, &byte, 1) ==
        AICE_CPU_INVALID_ARGUMENT);
    CHECK(aice_cpu_batch_crypt(128, 1, AICE_CPU_ENCRYPT, NULL, iv, &byte, &byte, 1) == AICE_CPU_INVALID_ARGUMENT);
    CHECK(aice_cpu_batch_crypt(128, 1, AICE_CPU_ENCRYPT, key, iv, NULL, NULL, 0) == AICE_CPU_SUCCESS);
    return EXIT_SUCCESS;
}

int main(void)
{
    CHECK(test_iv12_regression_vector() == EXIT_SUCCESS);
    CHECK(test_iv12_lane_index_high_word() == EXIT_SUCCESS);
    CHECK(test_roundtrip_and_in_place() == EXIT_SUCCESS);
    CHECK(test_invalid_arguments() == EXIT_SUCCESS);
    return EXIT_SUCCESS;
}
