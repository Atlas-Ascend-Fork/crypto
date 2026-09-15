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

foreach(bits 128 256)
    add_executable(shake${bits}_tiling_test test_shake_tensor_tiling.cpp)
    target_compile_definitions(shake${bits}_tiling_test PRIVATE SHAKE_TEST_BITS=${bits})
    target_include_directories(shake${bits}_tiling_test PRIVATE "${KECCAK_DIR}/tests/ut/op_host/stubs")
    target_link_libraries(shake${bits}_tiling_test PRIVATE cann_crypto_shake_host)
    cann_warnings(shake${bits}_tiling_test)
    add_test(NAME shake${bits}_tiling_test COMMAND shake${bits}_tiling_test)
endforeach()
