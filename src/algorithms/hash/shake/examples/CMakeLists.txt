# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

add_executable(example_shake_tensor test_aclnn_shake_tensor.cpp)
target_include_directories(example_shake_tensor PRIVATE "${KECCAK_DIR}/examples")
target_link_libraries(example_shake_tensor PRIVATE shake_runtime)
cann_warnings(example_shake_tensor)
if(CANN_BUILD_TESTS)
    cann_device_test(example_shake_tensor shake 128)
    cann_device_test(example_shake_tensor shake 256)
endif()
