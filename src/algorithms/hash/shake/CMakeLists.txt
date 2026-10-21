# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.16)
get_filename_component(KECCAK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../primitives/keccak" ABSOLUTE)
include("${KECCAK_DIR}/cmake/CannAscend.cmake")
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(cann_crypto_shake LANGUAGES C CXX)
    include(CTest)
endif()
if(CANN_BUILD_DEVICE)
    enable_language(ASC)
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
include("${KECCAK_DIR}/cmake/CannOperator.cmake")

add_library(cann_crypto_shake_host STATIC shake_launch.cpp)
set_target_properties(cann_crypto_shake_host PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(cann_crypto_shake_host PUBLIC cxx_std_17)
target_include_directories(cann_crypto_shake_host PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
cann_warnings(cann_crypto_shake_host)

if(NOT TARGET cann_crypto_keccak_host)
    set(CANN_CRYPTO_DEPENDENCY_ONLY ON)
    add_subdirectory("${KECCAK_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/keccak")
    unset(CANN_CRYPTO_DEPENDENCY_ONLY)
endif()
if(CANN_BUILD_DEVICE)
    cann_add_direct_library(shake shake_runtime.cpp op_kernel/shake_launch.asc)
    target_link_libraries(cann_crypto_shake PRIVATE cann_crypto_shake_host cann_crypto_keccak_host)
    add_subdirectory(examples)
endif()
if(CANN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
