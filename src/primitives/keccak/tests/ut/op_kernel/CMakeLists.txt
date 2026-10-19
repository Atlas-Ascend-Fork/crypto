# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

add_executable(keccak_reference_kat test_reference.c)
target_link_libraries(keccak_reference_kat PRIVATE cann_crypto_keccak_reference)
cann_warnings(keccak_reference_kat)
add_test(NAME keccak_reference_kat COMMAND keccak_reference_kat)

add_executable(keccak_permutation_model test_permutation.cpp)
target_compile_features(keccak_permutation_model PRIVATE cxx_std_17)
target_include_directories(keccak_permutation_model PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/ascend_model" "${CMAKE_CURRENT_SOURCE_DIR}/../../../../..")
target_link_libraries(keccak_permutation_model PRIVATE cann_crypto_keccak_reference)
if(NOT MSVC)
    target_compile_options(keccak_permutation_model PRIVATE -fno-strict-aliasing)
endif()
cann_warnings(keccak_permutation_model)
add_test(NAME keccak_permutation_model COMMAND keccak_permutation_model)

if(CANN_BUILD_CPU_DEBUG_TESTS)
    cann_kernel_unit_test(keccak_kernel_ut test_keccak_f1600_tensor.cpp)
endif()
