# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

add_executable(aes_host_ut ut/op_host/test_aes_host.cpp ../op_host/aes_host.cpp)
target_include_directories(aes_host_ut PRIVATE
    "${CANN_AES_REPO_ROOT}/include" "${CMAKE_CURRENT_SOURCE_DIR}/ut/op_host/stubs")
target_compile_options(aes_host_ut PRIVATE -Wall -Wextra -Werror)
add_test(NAME aes_host_ut COMMAND aes_host_ut)
set_tests_properties(aes_host_ut PROPERTIES LABELS "host;ut")
if(CANN_BUILD_DEVICE)
    find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
    add_test(NAME aes_native_st COMMAND "${CMAKE_COMMAND}" -E env
        "LD_LIBRARY_PATH=${ASCEND_CANN_PACKAGE_PATH}/lib64:$ENV{LD_LIBRARY_PATH}"
        "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/st/test_aes_native.py"
        --library "$<TARGET_FILE:cann_crypto_aes>" --device "${CANN_DEVICE_ID}")
    set_tests_properties(aes_native_st PROPERTIES LABELS "device;st" RESOURCE_LOCK ascend_device TIMEOUT 600)
endif()
