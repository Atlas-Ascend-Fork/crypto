# ----------------------------------------------------------------------------
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(cann_crypto_ntt LANGUAGES CXX)
    include(CTest)
endif()
option(CRYPTO_NTT_DEVICE "Build the NTT kernel-launch library using the installed CANN SDK" OFF)
option(CANN_BUILD_CPU_DEBUG_TESTS "Build Kernel UT with GTest and tikicpulib" OFF)
set(CANN_CPU_DEBUG_SOC "Ascend910B1" CACHE STRING "Full SoC name used by tikicpulib")
set(CANN_KERNEL_UT_TIMEOUT 1800 CACHE STRING "Timeout in seconds for each Kernel UT executable")
set(RUN_MODE "npu" CACHE STRING "Kernel-launch execution mode: npu or sim")
set_property(CACHE RUN_MODE PROPERTY STRINGS npu sim)
set(SOC_VERSION "Ascend910B1" CACHE STRING "Concrete chip model for kernel launch")
set(ASCEND_CANN_PACKAGE_PATH "" CACHE PATH "CANN SDK root for kernel-launch compilation")
if(CRYPTO_NTT_DEVICE)
    if(NOT RUN_MODE STREQUAL "npu" AND NOT RUN_MODE STREQUAL "sim")
        message(FATAL_ERROR "RUN_MODE must be npu or sim; Host tests use CRYPTO_NTT_DEVICE=OFF")
    endif()
    enable_language(C)
endif()

add_library(cann_crypto_ntt STATIC op_host/ntt_launch.cpp op_host/ntt_tiling.cpp)
target_compile_features(cann_crypto_ntt PUBLIC cxx_std_17)
target_include_directories(cann_crypto_ntt PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/op_host")
set_target_properties(cann_crypto_ntt PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(cann_crypto_ntt PRIVATE -Wall -Wextra -Wpedantic -Werror)

if(CANN_BUILD_CPU_DEBUG_TESTS AND NOT BUILD_TESTING)
    message(FATAL_ERROR "CANN_BUILD_CPU_DEBUG_TESTS requires BUILD_TESTING=ON")
endif()
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

if(CRYPTO_NTT_DEVICE)
    include(cmake/npu_lib.cmake)
endif()
