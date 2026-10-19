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
include(cmake/CannAscend.cmake)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(cann_crypto_keccak LANGUAGES C CXX)
    include(CTest)
endif()
if(CANN_BUILD_DEVICE)
    enable_language(ASC)
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
include(cmake/CannOperator.cmake)

add_library(cann_crypto_keccak_host INTERFACE)
target_include_directories(cann_crypto_keccak_host INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

if(CANN_BUILD_DEVICE)
    cann_add_direct_library(keccak keccak_launch.cpp op_kernel/keccak_f1600_launch.asc)
    if(NOT CANN_CRYPTO_DEPENDENCY_ONLY)
        add_subdirectory(examples)
    endif()
endif()
if(CANN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
