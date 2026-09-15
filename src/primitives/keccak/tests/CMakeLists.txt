# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

add_library(cann_crypto_keccak_reference STATIC assets/sha3_ref.c)
target_compile_features(cann_crypto_keccak_reference PUBLIC c_std_11)
target_include_directories(cann_crypto_keccak_reference PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/assets")
cann_warnings(cann_crypto_keccak_reference)

add_subdirectory(ut/op_host)
add_subdirectory(ut/op_kernel)
add_test(NAME keccak_golden COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/assets/golden.py")
add_test(NAME keccak_build_tools COMMAND "${CMAKE_COMMAND}" -E env
    "CANN_TEST_CMAKE=${CMAKE_COMMAND}" "CANN_TEST_GENERATOR=${CMAKE_GENERATOR}"
    "CANN_TEST_C_COMPILER=${CMAKE_C_COMPILER}" "CANN_TEST_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
    "CANN_TEST_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
    "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_build.py")
if(CANN_BUILD_DEVICE AND NOT CANN_CRYPTO_DEPENDENCY_ONLY)
    add_subdirectory(st)
endif()
