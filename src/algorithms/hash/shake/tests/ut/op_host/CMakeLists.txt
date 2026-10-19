# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

add_executable(shake_launch_config_test test_launch_config.cpp)
target_link_libraries(shake_launch_config_test PRIVATE cann_crypto_shake_host)
cann_warnings(shake_launch_config_test)
add_test(NAME shake_launch_config_test COMMAND shake_launch_config_test)

add_executable(shake_direct_launch_test
    "${KECCAK_DIR}/tests/ut/op_host/test_direct_launch.cpp"
    "${KECCAK_DIR}/keccak_launch.cpp" ../../../shake_runtime.cpp)
target_compile_definitions(shake_direct_launch_test PRIVATE CANN_CRYPTO_TEST_SHAKE=1)
target_include_directories(shake_direct_launch_test PRIVATE
    "${KECCAK_DIR}/tests/ut/op_host/stubs" "${CANN_CRYPTO_INCLUDE_DIR}")
target_link_libraries(shake_direct_launch_test PRIVATE cann_crypto_shake_host)
cann_warnings(shake_direct_launch_test)
add_test(NAME shake_direct_launch_test COMMAND shake_direct_launch_test)
