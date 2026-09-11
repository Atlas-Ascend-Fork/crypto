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
#include <cstring>

#ifdef ASCENDC_RESTORE_CONSTEXPR_KEYWORD
#undef constexpr
#undef ASCENDC_RESTORE_CONSTEXPR_KEYWORD
#endif

#if defined(ASCENDC_CPU_DEBUG) && !defined(GET_TILING_DATA_WITH_STRUCT)
#define GET_TILING_DATA_WITH_STRUCT(tilingStruct, tilingData, tilingArg) \
    tilingStruct tilingData;                                             \
    std::memcpy(&tilingData, tilingArg, sizeof(tilingStruct))
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
#define __check_diff_vec(x, y, len)                                                                                  \
    {                                                                                                                \
        int32_t __check_diff_num = 0;                                                                                \
        auto __check_diff_x = x[0];                                                                                  \
        auto __check_diff_y = y[0];                                                                                  \
        int32_t __check_diff_line = -1;                                                                              \
        for (int32_t i = 0; i < len; i++) {                                                                          \
            if (x[i] != y[i]) {                                                                                      \
                if (__check_diff_num++ == 0) {                                                                       \
                    __check_diff_x = x[i];                                                                           \
                    __check_diff_y = y[i];                                                                           \
                    __check_diff_line = i;                                                                           \
                }                                                                                                    \
            }                                                                                                        \
        }                                                                                                            \
        if (__check_diff_num ==                                                                                      \
            0) /*AscendC::printf("\033[1;92mNo diff between %s and %s\033[0m %s:%d\n", #x, #y, __FILE__, __LINE__)*/ \
            ;                                                                                                        \
        else {                                                                                                       \
            AscendC::printf(                                                                                         \
                "[%ld]\033[1;31m%d diff between %s and %s(%d), earliest = (%8lx, %8lx at %d)\033[0m %s:%d\n",        \
                AscendC::GetBlockIdx(), __check_diff_num, #x, #y, len, __check_diff_x, __check_diff_y,               \
                __check_diff_line, __FILE__, __LINE__);                                                              \
        }                                                                                                            \
    }
#define __check_diff_catch(x, y, len, _catch)                                                                        \
    {                                                                                                                \
        int32_t __check_diff_num = 0;                                                                                \
        auto __check_diff_x = x.GetValue(0);                                                                         \
        auto __check_diff_y = y[0];                                                                                  \
        int32_t __check_diff_line = -1;                                                                              \
        for (int32_t i = 0; i < len; i++) {                                                                          \
            if (x.GetValue(i) != y[i]) {                                                                             \
                if (__check_diff_num++ == 0) {                                                                       \
                    __check_diff_x = x.GetValue(i);                                                                  \
                    __check_diff_y = y[i];                                                                           \
                    __check_diff_line = i;                                                                           \
                }                                                                                                    \
            }                                                                                                        \
        }                                                                                                            \
        if (__check_diff_num ==                                                                                      \
            0) /*AscendC::printf("\033[1;92mNo diff between %s and %s\033[0m %s:%d\n", #x, #y, __FILE__, __LINE__)*/ \
            ;                                                                                                        \
        else {                                                                                                       \
            AscendC::printf(                                                                                         \
                "[%ld]\033[1;31m%d diff between %s and %s(%d), earliest = (%d, %d at %d)\033[0m %s:%d\n",            \
                AscendC::GetBlockIdx(), __check_diff_num, #x, #y, len, __check_diff_x, __check_diff_y,               \
                __check_diff_line, __FILE__, __LINE__);                                                              \
            {                                                                                                        \
                _catch;                                                                                              \
            }                                                                                                        \
        }                                                                                                            \
    }

#define __check_diff(x, y, len) __check_diff_catch(x, y, len, { (void)0; })

#define __check_diff_t64_catch(x, y, len, _catch)                                                                      \
    {                                                                                                                  \
        int32_t __check_diff_num = 0;                                                                                  \
        int64_t __check_diff_x = x.concat(0), __check_diff_y = y[0];                                                   \
        int32_t __check_diff_line = -1;                                                                                \
        for (int32_t i = 0; i < len; i++) {                                                                            \
            if (x.concat(i) != y[i]) {                                                                                 \
                if (__check_diff_num++ == 0) {                                                                         \
                    __check_diff_x = x.concat(i);                                                                      \
                    __check_diff_y = y[i];                                                                             \
                    __check_diff_line = i;                                                                             \
                }                                                                                                      \
            }                                                                                                          \
        }                                                                                                              \
        if (__check_diff_num == 0)                                                                                     \
            AscendC::printf("\033[1;92mNo diff between %s and %s\033[0m %s:%d\n", #x, #y, __FILE__, __LINE__);         \
        else {                                                                                                         \
            AscendC::printf(                                                                                           \
                "\033[1;31m%d diff between %s and %s, earliest = (%x, %x at %d)\033[0m %s:%d\n", __check_diff_num, #x, \
                #y, __check_diff_x, __check_diff_y, __check_diff_line, __FILE__, __LINE__);                            \
            {                                                                                                          \
                _catch;                                                                                                \
            }                                                                                                          \
        }                                                                                                              \
    }
#define __check_diff_t64(x, y, len) __check_diff_t64_catch(x, y, len, { (void)0; })

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
