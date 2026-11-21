# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
cmake_minimum_required(VERSION 3.16)
option(CANN_BUILD_DEVICE "Build independent SM3 Ascend service" ON)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(cann_crypto_sm3 VERSION 0.1.0 LANGUAGES C CXX)
    include(CTest)
endif()
# CANN 9.0 config.cmake clears normal C++ configuration flags. Preserve the
# caller's initialized flags locally; do not change cache entries or siblings.
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(_sm3_cxx_flags_${config} "${CMAKE_CXX_FLAGS_${config}}")
endforeach()
option(CANN_BUILD_TESTS "Build SM3 tests" ${BUILD_TESTING})
get_filename_component(SM3_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
if(NOT CANN_BUILD_DEVICE)
    if(CANN_BUILD_TESTS)
        enable_testing()
        add_subdirectory(tests)
    endif()
    return()
endif()
if(CANN_BUILD_DEVICE)
    if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "SM3 device builds require Linux and CANN")
    endif()
    if(DEFINED ASCEND_COMPUTE_UNIT AND NOT ASCEND_COMPUTE_UNIT STREQUAL "ascend910b")
        message(FATAL_ERROR "SM3 device validation currently targets Ascend910B only")
    endif()
    find_package(ASC REQUIRED HINTS
        "$ENV{ASCEND_HOME_PATH}/compiler/tikcpp/ascendc_kernel_cmake"
        "$ENV{ASCEND_TOOLKIT_HOME}/compiler/tikcpp/ascendc_kernel_cmake")
endif()
if(CANN_BUILD_DEVICE)
    enable_language(ASC)
    if(NOT CMAKE_SHARED_LIBRARY_RUNTIME_ASC_FLAG)
        set(CMAKE_SHARED_LIBRARY_RUNTIME_ASC_FLAG "-Wl,-rpath,")
        set(CMAKE_SHARED_LIBRARY_RUNTIME_ASC_FLAG_SEP ":")
    endif()
endif()
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_CXX_FLAGS_${config} "${_sm3_cxx_flags_${config}}")
endforeach()
option(CANN_SM3_SCALAR_ONLY "Use the scalar word backend for all batches" OFF)
set(SM3_VECTOR_TILE 128 CACHE STRING "Messages per vector tile (32, 64, 128 or 256)")
add_library(cann_crypto_sm3_service SHARED "${SM3_REPO_ROOT}/src/api/sm3.cpp")
add_library(CannCrypto::sm3 ALIAS cann_crypto_sm3_service)
target_compile_features(cann_crypto_sm3_service PRIVATE cxx_std_17)
target_compile_definitions(cann_crypto_sm3_service PRIVATE CANN_CRYPTO_SM3_BUILDING=1
    SM3_VECTOR_TILE=${SM3_VECTOR_TILE}
    CANN_CRYPTO_SM3_VECTOR_ENABLED=$<NOT:$<BOOL:${CANN_SM3_SCALAR_ONLY}>>)
target_include_directories(cann_crypto_sm3_service PUBLIC
    $<BUILD_INTERFACE:${SM3_REPO_ROOT}/include> $<INSTALL_INTERFACE:include>)
set_target_properties(cann_crypto_sm3_service PROPERTIES OUTPUT_NAME cann_crypto_sm3
    VERSION 0.1.0 SOVERSION 0 CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
if(CANN_BUILD_DEVICE)
    target_sources(cann_crypto_sm3_service PRIVATE op_host/sm3_runtime.cpp op_kernel/sm3_batch.asc)
    target_sources(cann_crypto_sm3_service PRIVATE op_kernel/sm3_vector_batch.asc op_kernel/sm3_vector_wide.asc)
    if(CANN_BUILD_TESTS)
        target_sources(cann_crypto_sm3_service PRIVATE op_kernel/u32_probe.asc)
    endif()
    target_compile_definitions(cann_crypto_sm3_service PRIVATE CANN_CRYPTO_SM3_HAS_NPU=1)
    target_compile_options(cann_crypto_sm3_service PRIVATE $<$<COMPILE_LANGUAGE:ASC>:--npu-arch=dav-2201>)
    target_include_directories(cann_crypto_sm3_service PRIVATE
        "$ENV{ASCEND_HOME_PATH}/include" "$ENV{ASCEND_HOME_PATH}/include/experiment"
        "$ENV{ASCEND_TOOLKIT_HOME}/include" "$ENV{ASCEND_TOOLKIT_HOME}/include/experiment")
    target_link_directories(cann_crypto_sm3_service PRIVATE
        "$ENV{ASCEND_HOME_PATH}/lib64" "$ENV{ASCEND_TOOLKIT_HOME}/lib64")
    target_link_libraries(cann_crypto_sm3_service PRIVATE ascendcl platform tiling_api)
    set_target_properties(cann_crypto_sm3_service PROPERTIES LINKER_LANGUAGE ASC)
endif()
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
install(TARGETS cann_crypto_sm3_service EXPORT CannCryptoSm3Targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
set_target_properties(cann_crypto_sm3_service PROPERTIES EXPORT_NAME sm3)
install(FILES "${SM3_REPO_ROOT}/include/cann_crypto/sm3.h" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cann_crypto)
install(FILES "${SM3_REPO_ROOT}/LICENSE" DESTINATION ${CMAKE_INSTALL_DATADIR}/CannCryptoSm3)
install(EXPORT CannCryptoSm3Targets NAMESPACE CannCrypto:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CannCryptoSm3)
configure_package_config_file(cmake/CannCryptoSm3Config.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/CannCryptoSm3Config.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CannCryptoSm3)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/CannCryptoSm3ConfigVersion.cmake"
    VERSION 0.1.0 COMPATIBILITY SameMinorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/CannCryptoSm3Config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/CannCryptoSm3ConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CannCryptoSm3)
if(CANN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
