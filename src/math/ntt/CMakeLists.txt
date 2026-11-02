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
option(CANN_BUILD_DEVICE "Build the direct NTT kernel library" OFF)
option(CANN_BUILD_TESTS "Build NTT tests" ON)
set(ASCEND_COMPUTE_UNIT ascend910b CACHE STRING "Target product: ascend910b (A2) or ascend910_93 (A3)")
set_property(CACHE ASCEND_COMPUTE_UNIT PROPERTY STRINGS ascend910b ascend910_93)
set(ASCEND_CANN_PACKAGE_PATH "$ENV{ASCEND_HOME_PATH}" CACHE PATH "CANN installation directory")
set(RUN_MODE "npu" CACHE STRING "Execution mode: npu or sim")
set_property(CACHE RUN_MODE PROPERTY STRINGS npu sim)
if(CANN_BUILD_DEVICE)
    if(NOT ASCEND_COMPUTE_UNIT MATCHES "^(ascend910b|ascend910_93)$")
        message(FATAL_ERROR "Supported compute units: ascend910b (A2), ascend910_93 (A3)")
    endif()
    if(NOT RUN_MODE STREQUAL "npu" AND NOT RUN_MODE STREQUAL "sim")
        message(FATAL_ERROR "RUN_MODE must be npu or sim")
    endif()
    if(RUN_MODE STREQUAL "sim" AND NOT ASCEND_COMPUTE_UNIT STREQUAL "ascend910b")
        message(FATAL_ERROR "Local NTT Camodel validation uses the Ascend910B1 model (ascend910b)")
    endif()
    set(CMAKE_ASC_ARCHITECTURES dav-2201 CACHE STRING "NPU architecture")
    if(NOT CMAKE_ASC_ARCHITECTURES STREQUAL "dav-2201")
        message(FATAL_ERROR "NTT currently requires CMAKE_ASC_ARCHITECTURES=dav-2201")
    endif()
    list(PREPEND CMAKE_PREFIX_PATH "${ASCEND_CANN_PACKAGE_PATH}")
    find_package(ASC REQUIRED)
endif()
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(cann_crypto_ntt LANGUAGES C CXX)
    include(CTest)
endif()
if(CANN_BUILD_DEVICE)
    enable_language(ASC)
endif()
option(CANN_BUILD_CPU_DEBUG_TESTS "Build Kernel UT with GTest and tikicpulib" OFF)
set(CANN_CPU_DEBUG_SOC "Ascend910B1" CACHE STRING "Full SoC name used by tikicpulib")
set(CANN_KERNEL_UT_TIMEOUT 1800 CACHE STRING "Timeout in seconds for each Kernel UT executable")

add_library(cann_crypto_ntt STATIC op_host/ntt_launch.cpp op_host/ntt_tiling.cpp)
target_compile_features(cann_crypto_ntt PUBLIC cxx_std_17)
target_include_directories(cann_crypto_ntt PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/op_host")
set_target_properties(cann_crypto_ntt PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(cann_crypto_ntt PRIVATE -Wall -Wextra -Wpedantic -Werror)

if(CANN_BUILD_CPU_DEBUG_TESTS AND NOT CANN_BUILD_TESTS)
    message(FATAL_ERROR "CANN_BUILD_CPU_DEBUG_TESTS requires CANN_BUILD_TESTS=ON")
endif()
if(CANN_BUILD_DEVICE)
    include(cmake/npu_lib.cmake)
endif()
if(CANN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
