/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef NTT_MACROS_H_
#define NTT_MACROS_H_

#if defined(ASCENDC_CPU_DEBUG) && defined(__clang__)
// CANN 8.5.0 tikicpulib declares a constexpr function returning AscendC::half,
// but the CPU-debug half type is not literal under Clang 15. Strip constexpr
// only while including the vendor CPU header; NPU/AscendC compilation is left
// untouched, and the keyword is restored before project code is parsed.
#define ASCENDC_RESTORE_CONSTEXPR_KEYWORD
#define constexpr
#endif

#include "kernel_operator.h"
#ifdef ASCENDC_CPU_DEBUG
#include "securec.h"
#endif

#ifdef ASCENDC_RESTORE_CONSTEXPR_KEYWORD
#undef constexpr
#undef ASCENDC_RESTORE_CONSTEXPR_KEYWORD
#endif

#ifdef ASCENDC_CPU_DEBUG
template <typename T>
inline bool ReadCpuTiling(T& destination, const void* source)
{
    if (source == nullptr) {
        return false;
    }
    return memcpy_s(&destination, sizeof(destination), source, sizeof(T)) == EOK;
}
#endif

#if defined(ASCENDC_CPU_DEBUG) && !defined(GET_TILING_DATA_WITH_STRUCT)
#define GET_TILING_DATA_WITH_STRUCT(tilingStruct, tilingData, tilingArg) \
    tilingStruct tilingData{};                                           \
    if (!ReadCpuTiling(tilingData, tilingArg)) {                         \
        AscendC::printf("invalid NTT tiling data\n");                    \
        return;                                                          \
    }
#endif

#if defined(ASCENDC_CPU_DEBUG) && !defined(REGISTER_TILING_DEFAULT)
#define REGISTER_TILING_DEFAULT(tilingStruct)
#endif

using AscendC::LocalTensor;

using t32 = AscendC::LocalTensor<int32_t>;

#ifdef ASCENDC_CPU_DEBUG
#define __print_tensor_short(x, len, line)                                                                   \
    {                                                                                                        \
        AscendC::printf(                                                                                     \
            "[%ld]\033[1;32mtensor[%s] \033[0mlen = %d, %s:%d\n", AscendC::GetBlockIdx(), #x, len, __FILE__, \
            __LINE__);                                                                                       \
        for (uint32_t i = 0; i < (uint32_t)len; i++) {                                                       \
            AscendC::printf("%4d ", x.GetValue(i));                                                          \
            if ((i + 1) % line == 0)                                                                         \
                AscendC::printf("\n");                                                                       \
        }                                                                                                    \
        AscendC::printf("\n");                                                                               \
    }
#define __print_vector(x, len)                                                          \
    {                                                                                   \
        AscendC::printf("\033[1;32mvector[%s] %s:%d\033[0m\n", #x, __FILE__, __LINE__); \
        for (uint32_t i = 0; i < (uint32_t)len; i++) {                                  \
            AscendC::printf("%11d ", x[i]);                                             \
            if (i % 8 == 7)                                                             \
                AscendC::printf("\n");                                                  \
        }                                                                               \
        AscendC::printf("\n");                                                          \
    }
#define NTT_CHECK_DIFF(x, y, len, read_value, action)                                                             \
    do {                                                                                                          \
        int32_t differences = 0;                                                                                  \
        int32_t firstIndex = -1;                                                                                  \
        int64_t firstActual = 0;                                                                                  \
        int64_t firstExpected = 0;                                                                                \
        for (int32_t i = 0; i < (len); ++i) {                                                                     \
            const auto actual = (read_value);                                                                     \
            if (actual != (y)[i]) {                                                                               \
                if (firstIndex < 0) {                                                                             \
                    firstIndex = i;                                                                               \
                    firstActual = actual;                                                                         \
                    firstExpected = (y)[i];                                                                       \
                }                                                                                                 \
                ++differences;                                                                                    \
            }                                                                                                     \
        }                                                                                                         \
        if (differences != 0) {                                                                                   \
            AscendC::printf(                                                                                      \
                "%d diff between %s and %s, first=(%lld,%lld at %d), %s:%d\n", differences, #x, #y,               \
                static_cast<long long>(firstActual), static_cast<long long>(firstExpected), firstIndex, __FILE__, \
                __LINE__);                                                                                        \
            action;                                                                                               \
        }                                                                                                         \
    } while (0)

#define __check_diff_vec(x, y, len) NTT_CHECK_DIFF(x, y, len, (x)[i], (void)0)
#define __check_diff_catch(x, y, len, action) NTT_CHECK_DIFF(x, y, len, (x).GetValue(i), action)
#define __check_diff(x, y, len) __check_diff_catch(x, y, len, (void)0)
#define __check_diff_t64_catch(x, y, len, action) NTT_CHECK_DIFF(x, y, len, (x).concat(i), action)
#define __check_diff_t64(x, y, len) __check_diff_t64_catch(x, y, len, (void)0)

#define __assertion_fail(msg, ...)                                        \
    {                                                                     \
        AscendC::printf("[%ld]\033[1;31mFAIL: ", AscendC::GetBlockIdx()); \
        AscendC::printf(msg, ##__VA_ARGS__);                              \
        AscendC::printf("\033[0m %s:%d\n", __FILE__, __LINE__);           \
    }

#define __assertion_info(msg, ...)                                        \
    {                                                                     \
        AscendC::printf("[%ld]\033[1;92mINFO: ", AscendC::GetBlockIdx()); \
        AscendC::printf(msg, ##__VA_ARGS__);                              \
        AscendC::printf("\033[0m %s:%d\n", __FILE__, __LINE__);           \
    }

#else

#define __print_tensor_short(x, len, line) ;
#define __print_vector(x, len) ;
#define __check_diff_catch(x, y, len, _catch) ;
#define __check_diff(x, y, len) ;
#define __check_diff_t64_catch(x, y, len, _catch) ;
#define __check_diff_t64(x, y, len) ;
#define __assertion_fail(msg, ...) ;
#define __assertion_info(msg, ...) ;

#endif

#endif
