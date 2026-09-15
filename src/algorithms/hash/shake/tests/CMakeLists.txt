# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

add_subdirectory(ut/op_host)
add_subdirectory(ut/op_kernel)
add_test(NAME shake_golden COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/assets/golden.py")
if(CANN_BUILD_DEVICE)
    find_package(OpenSSL 1.1.1 REQUIRED COMPONENTS Crypto)
    add_executable(shake_device_test st/test_aclnn_sha3.cpp)
    target_link_libraries(shake_device_test PRIVATE shake_runtime cann_crypto_keccak_reference OpenSSL::Crypto)
    cann_warnings(shake_device_test)
    cann_device_test(shake_device_test shake)
endif()
