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
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(cann_crypto_keccak LANGUAGES C CXX)
    include(CTest)
endif()
include(cmake/CannOperator.cmake)

if(CANN_BUILD_DEVICE AND NOT CANN_CRYPTO_DEPENDENCY_ONLY)
    cann_add_package(keccak "${CMAKE_CURRENT_SOURCE_DIR}")
endif()
if(CANN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
if(CANN_BUILD_DEVICE AND NOT CANN_CRYPTO_DEPENDENCY_ONLY)
    add_subdirectory(examples)
endif()
