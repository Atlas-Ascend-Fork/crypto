# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

add_executable(keccak_launch_config_test test_launch_config.cpp)
target_link_libraries(keccak_launch_config_test PRIVATE cann_crypto_keccak_host)
cann_warnings(keccak_launch_config_test)
add_test(NAME keccak_launch_config_test COMMAND keccak_launch_config_test)

add_executable(crypto_direct_launch_test test_direct_launch.cpp ../../../keccak_launch.cpp)
target_compile_features(crypto_direct_launch_test PRIVATE cxx_std_17)
target_include_directories(crypto_direct_launch_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs" "${CANN_CRYPTO_INCLUDE_DIR}")
cann_warnings(crypto_direct_launch_test)
add_test(NAME crypto_direct_launch_test COMMAND crypto_direct_launch_test)

add_executable(crypto_public_headers_test test_public_headers.c)
target_include_directories(crypto_public_headers_test PRIVATE "${CANN_CRYPTO_INCLUDE_DIR}")
cann_warnings(crypto_public_headers_test)
add_test(NAME crypto_public_headers_test COMMAND crypto_public_headers_test)
