# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
add_subdirectory(ut)
if(CANN_BUILD_DEVICE)
    if(RUN_MODE STREQUAL "npu")
        add_executable(ntt_launch_test st/test_ntt_launch.cpp)
        target_link_libraries(ntt_launch_test PRIVATE cann_crypto_ntt_device ascendcl)
        add_test(NAME ntt_device_launch COMMAND ntt_launch_test)
        set_tests_properties(ntt_device_launch PROPERTIES LABELS "st;device")
    endif()
    foreach(profile ml-kem ml-dsa rns)
        add_test(NAME ntt_device_${profile} COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/run_example.sh"
            "$<TARGET_FILE:ntt_kernellaunch_example>" "${ASCEND_CANN_PACKAGE_PATH}" "${RUN_MODE}" "${ASCEND_COMPUTE_UNIT}")
        set_tests_properties(ntt_device_${profile} PROPERTIES LABELS "st;device"
            ENVIRONMENT "NTT_PROFILE=${profile};NTT_PYTHON=${Python3_EXECUTABLE}")
    endforeach()
endif()
