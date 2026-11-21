# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.

add_executable(sm3_api_validation ut/op_host/test_sm3_api_validation.cpp "${SM3_REPO_ROOT}/src/api/sm3.cpp")
target_compile_features(sm3_api_validation PRIVATE cxx_std_17)
target_compile_definitions(sm3_api_validation PRIVATE CANN_CRYPTO_SM3_BUILDING=1)
target_include_directories(sm3_api_validation PRIVATE "${SM3_REPO_ROOT}/include" "${CMAKE_CURRENT_SOURCE_DIR}/..")
add_test(NAME sm3_api_validation COMMAND sm3_api_validation)

if(NOT CANN_BUILD_DEVICE)
    return()
endif()

if(NOT TARGET cann_crypto_sm3_reference)
    add_subdirectory("${SM3_REPO_ROOT}/reference/sm3" "${CMAKE_CURRENT_BINARY_DIR}/reference")
endif()
if(CANN_BUILD_DEVICE)
    add_executable(sm3_direct_device_test st/test_sm3_direct.cpp)
    target_compile_features(sm3_direct_device_test PRIVATE cxx_std_17)
    target_include_directories(sm3_direct_device_test PRIVATE "$ENV{ASCEND_HOME_PATH}/include")
    target_link_directories(sm3_direct_device_test PRIVATE "$ENV{ASCEND_HOME_PATH}/lib64")
    target_link_libraries(sm3_direct_device_test PRIVATE CannCrypto::sm3 cann_crypto_sm3_reference ascendcl)
    if(CRYPTO_SM3_OPENSSL_TESTS)
        target_compile_definitions(sm3_direct_device_test PRIVATE SM3_ORACLE_OPENSSL=1)
        target_link_libraries(sm3_direct_device_test PRIVATE OpenSSL::Crypto)
    endif()
    add_test(NAME sm3_direct_device COMMAND sm3_direct_device_test)
    set_tests_properties(sm3_direct_device PROPERTIES LABELS device RESOURCE_LOCK ascend_device TIMEOUT 600)
endif()

if(CANN_BUILD_DEVICE)
    add_executable(sm3_u32_probe_test st/test_u32_probe.cpp)
    target_compile_features(sm3_u32_probe_test PRIVATE cxx_std_17)
    target_include_directories(sm3_u32_probe_test PRIVATE "$ENV{ASCEND_HOME_PATH}/include")
    target_link_directories(sm3_u32_probe_test PRIVATE "$ENV{ASCEND_HOME_PATH}/lib64")
    target_link_libraries(sm3_u32_probe_test PRIVATE CannCrypto::sm3 ascendcl)
    add_test(NAME sm3_u32_probe COMMAND sm3_u32_probe_test)
    set_tests_properties(sm3_u32_probe PROPERTIES LABELS device RESOURCE_LOCK ascend_device TIMEOUT 60)
endif()

if(CANN_BUILD_DEVICE)
    target_compile_definitions(sm3_direct_device_test PRIVATE SM3_TEST_VECTOR_RAW=1)
    add_test(NAME sm3_vector_forced COMMAND sm3_direct_device_test --vector)
    set_tests_properties(sm3_vector_forced PROPERTIES LABELS device RESOURCE_LOCK ascend_device TIMEOUT 600)
endif()
