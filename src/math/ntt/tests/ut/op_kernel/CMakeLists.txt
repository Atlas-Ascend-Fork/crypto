# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "NTT Kernel UT requires Linux and the CANN CPU debugging library")
endif()
if(NOT CANN_KERNEL_UT_TIMEOUT MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "CANN_KERNEL_UT_TIMEOUT must be a positive integer in seconds")
endif()
find_package(GTest REQUIRED)
find_package(tikicpulib CONFIG REQUIRED HINTS
    "${ASCEND_CANN_PACKAGE_PATH}/tools/tikicpulib/lib/cmake"
    "${ASCEND_CANN_PACKAGE_PATH}/toolkit/tools/tikicpulib/lib/cmake")
if(NOT TARGET tikicpulib::${CANN_CPU_DEBUG_SOC})
    message(FATAL_ERROR "tikicpulib does not provide target ${CANN_CPU_DEBUG_SOC}")
endif()

add_executable(ntt_kernel_ut test_ntt.cpp)
target_compile_features(ntt_kernel_ut PRIVATE cxx_std_17)
target_compile_definitions(ntt_kernel_ut PRIVATE _GLIBCXX_USE_CXX11_ABI=0)
target_include_directories(ntt_kernel_ut PRIVATE "${ASCEND_CANN_PACKAGE_PATH}/include")
target_link_directories(ntt_kernel_ut PRIVATE "${ASCEND_CANN_PACKAGE_PATH}/lib64")
target_link_libraries(ntt_kernel_ut PRIVATE GTest::Main tikicpulib::${CANN_CPU_DEBUG_SOC} ascendcl)
add_test(NAME ntt_kernel_ut COMMAND ntt_kernel_ut)
set_tests_properties(ntt_kernel_ut PROPERTIES LABELS "ut;kernel_ut" TIMEOUT ${CANN_KERNEL_UT_TIMEOUT})
