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
option(CANN_BUILD_DEVICE "Build the direct AES kernel library" OFF)
option(CANN_BUILD_TESTS "Build AES tests" ON)
set(ASCEND_CANN_PACKAGE_PATH "$ENV{ASCEND_HOME_PATH}" CACHE PATH "CANN installation directory")
set(CANN_DEVICE_ID 0 CACHE STRING "ACL logical device for tests")
if(DEFINED ENV{NPU_ARCH} AND NOT "$ENV{NPU_ARCH}" STREQUAL "")
    set(NPU_ARCH "$ENV{NPU_ARCH}" CACHE STRING "NPU architecture")
else()
    set(NPU_ARCH dav-2201 CACHE STRING "NPU architecture")
endif()
if(CANN_BUILD_DEVICE)
    if(NOT NPU_ARCH STREQUAL "dav-2201")
        message(FATAL_ERROR "AES currently provides kernels for NPU_ARCH=dav-2201")
    endif()
    list(PREPEND CMAKE_PREFIX_PATH "${ASCEND_CANN_PACKAGE_PATH}")
    set(CMAKE_ASC_ARCHITECTURES "${NPU_ARCH}" CACHE STRING "ASC architecture")
    find_package(ASC REQUIRED)
endif()
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(cann_crypto_aes LANGUAGES C CXX)
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(CANN_AES_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
if(CANN_BUILD_DEVICE)
    enable_language(ASC)
    add_library(cann_crypto_aes SHARED op_host/aes_host.cpp op_kernel/ctr.asc op_kernel/gcm.asc)
    set_target_properties(cann_crypto_aes PROPERTIES POSITION_INDEPENDENT_CODE ON LINKER_LANGUAGE ASC)
    target_include_directories(cann_crypto_aes PUBLIC
        "$<BUILD_INTERFACE:${CANN_AES_REPO_ROOT}/include>" "$<INSTALL_INTERFACE:include>")
    target_include_directories(cann_crypto_aes PRIVATE "${ASCEND_CANN_PACKAGE_PATH}/include")
    target_compile_options(cann_crypto_aes PRIVATE $<$<COMPILE_LANGUAGE:ASC>:--npu-arch=${NPU_ARCH}>)
    target_link_directories(cann_crypto_aes PRIVATE "${ASCEND_CANN_PACKAGE_PATH}/lib64")
    target_link_libraries(cann_crypto_aes PRIVATE ascendcl)
    add_executable(aes_gcm_example examples/aes_gcm_example.cpp)
    target_include_directories(aes_gcm_example PRIVATE "${ASCEND_CANN_PACKAGE_PATH}/include")
    target_link_directories(aes_gcm_example PRIVATE "${ASCEND_CANN_PACKAGE_PATH}/lib64")
    target_link_libraries(aes_gcm_example PRIVATE cann_crypto_aes ascendcl)
    install(TARGETS cann_crypto_aes LIBRARY DESTINATION lib)
    install(FILES "${CANN_AES_REPO_ROOT}/include/cann_crypto/aes.h" DESTINATION include/cann_crypto)
endif()
if(CANN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
