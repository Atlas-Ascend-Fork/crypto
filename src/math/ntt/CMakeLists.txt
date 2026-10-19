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

add_library(cann_crypto_ntt STATIC ntt_launch.cpp op_host/ntt_tiling.cpp)
target_compile_features(cann_crypto_ntt PUBLIC cxx_std_17)
target_include_directories(cann_crypto_ntt PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(cann_crypto_ntt PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(cann_crypto_ntt PRIVATE -Wall -Wextra -Wpedantic -Werror)

if(BUILD_TESTING)
    add_executable(ntt_host_test tests/host/test_ntt_host.cpp)
    target_link_libraries(ntt_host_test PRIVATE cann_crypto_ntt)
    target_compile_options(ntt_host_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME ntt_host COMMAND ntt_host_test)
    find_package(Python3 3.9 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME ntt_data COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/host/test_ntt_data.py")
        add_test(NAME ntt_runner COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/host/test_ntt_runner.py")
    endif()
endif()

if(CRYPTO_NTT_DEVICE)
    include(cmake/npu_lib.cmake)
endif()
