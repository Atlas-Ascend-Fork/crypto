# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

add_test(NAME shake_sponge_model COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_design_model.py")
add_library(shake_reference_test SHARED "${KECCAK_DIR}/tests/assets/sha3_ref.c")
set_target_properties(shake_reference_test PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
cann_warnings(shake_reference_test)
add_test(NAME shake_reference_hashlib COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_reference_hashlib.py" $<TARGET_FILE:shake_reference_test>)
find_package(OpenSSL 1.1.1 QUIET COMPONENTS Crypto)
if(OpenSSL_FOUND)
    add_executable(shake_reference_diff test_reference_diff.cpp)
    target_compile_features(shake_reference_diff PRIVATE cxx_std_17)
    target_link_libraries(shake_reference_diff PRIVATE cann_crypto_keccak_reference OpenSSL::Crypto)
    cann_warnings(shake_reference_diff)
    add_test(NAME shake_reference_diff COMMAND shake_reference_diff)
endif()
if(CANN_BUILD_CPU_DEBUG_TESTS)
    foreach(bits 128 256)
        cann_kernel_unit_test(shake${bits}_kernel_ut test_shake_tensor.cpp)
        target_compile_definitions(shake${bits}_kernel_ut PRIVATE SHAKE_TEST_BITS=${bits})
    endforeach()
endif()
