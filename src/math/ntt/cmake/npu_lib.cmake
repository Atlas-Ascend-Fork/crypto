# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

set(NTT_MODULE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
if(NOT SOC_VERSION MATCHES "^Ascend(910B[1234]|910B2C|910_93[0-9][0-9])$")
    message(FATAL_ERROR "Set SOC_VERSION to a concrete chip, e.g. Ascend910B1 or Ascend910_9382")
endif()
if(RUN_MODE STREQUAL "sim")
    if(NOT SOC_VERSION STREQUAL "Ascend910B1")
        message(FATAL_ERROR "Local NTT Camodel validation requires SOC_VERSION=Ascend910B1")
    endif()
endif()
if(NOT EXISTS "${ASCEND_CANN_PACKAGE_PATH}/include/acl/acl.h")
    message(FATAL_ERROR "Set ASCEND_CANN_PACKAGE_PATH to the installed CANN SDK")
endif()
find_path(NTT_ASCENDC_CMAKE_DIR ascendc.cmake
    PATHS "${ASCEND_CANN_PACKAGE_PATH}/compiler/tikcpp/ascendc_kernel_cmake"
          "${ASCEND_CANN_PACKAGE_PATH}/tools/tikcpp/ascendc_kernel_cmake"
    NO_DEFAULT_PATH REQUIRED)
include("${NTT_ASCENDC_CMAKE_DIR}/ascendc.cmake")

# Same kernel-launch build interface as the CANN introduction samples.
ascendc_library(ntt_kernels SHARED "${NTT_MODULE_DIR}/op_kernel/ntt.cpp")
add_library(cann_crypto_ntt_device SHARED "${NTT_MODULE_DIR}/op_host/ntt_npu_launch.cpp")
target_compile_features(cann_crypto_ntt_device PUBLIC cxx_std_17)
target_link_libraries(cann_crypto_ntt_device PRIVATE cann_crypto_ntt ntt_kernels host_intf_pub ascendcl)
target_include_directories(cann_crypto_ntt_device PUBLIC "${NTT_MODULE_DIR}")

add_executable(ntt_kernellaunch_example "${NTT_MODULE_DIR}/examples/main.cpp")
target_compile_features(ntt_kernellaunch_example PRIVATE cxx_std_17)
target_compile_options(ntt_kernellaunch_example PRIVATE -Wall -Wextra -Werror)
target_link_libraries(ntt_kernellaunch_example PRIVATE cann_crypto_ntt_device cann_crypto_ntt host_intf_pub ascendcl)
set_target_properties(ntt_kernels cann_crypto_ntt_device PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set_target_properties(ntt_kernellaunch_example PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(RUN_MODE STREQUAL "sim")
    set(NTT_SIM_LIB "${ASCEND_CANN_PACKAGE_PATH}/tools/simulator/${SOC_VERSION}/lib")
    if(NOT EXISTS "${NTT_SIM_LIB}/libruntime_camodel.so")
        message(FATAL_ERROR "Camodel is missing for ${SOC_VERSION}")
    endif()
    set(NTT_SIM_COMPAT "${CMAKE_BINARY_DIR}/simulator_compat")
    file(MAKE_DIRECTORY "${NTT_SIM_COMPAT}")
    file(CREATE_LINK "${NTT_SIM_LIB}/libruntime_camodel.so" "${NTT_SIM_COMPAT}/libruntime.so" SYMBOLIC)
    file(CREATE_LINK "${NTT_SIM_LIB}/libnpu_drv_camodel.so" "${NTT_SIM_COMPAT}/libascend_hal.so" SYMBOLIC)
    file(CREATE_LINK "${NTT_SIM_LIB}/libnpu_drv.so" "${NTT_SIM_COMPAT}/libnpu_drv.so" SYMBOLIC)
    target_link_directories(cann_crypto_ntt_device PRIVATE "${NTT_SIM_COMPAT}" "${NTT_SIM_LIB}")
    target_link_directories(ntt_kernellaunch_example PRIVATE "${NTT_SIM_COMPAT}" "${NTT_SIM_LIB}")
    target_link_libraries(cann_crypto_ntt_device PRIVATE runtime_camodel npu_drv_camodel)
    target_link_libraries(ntt_kernellaunch_example PRIVATE runtime_camodel npu_drv_camodel)
    target_link_options(ntt_kernellaunch_example PRIVATE -Wl,--allow-shlib-undefined)
endif()

install(TARGETS cann_crypto_ntt cann_crypto_ntt_device ntt_kernels ntt_kernellaunch_example
    LIBRARY DESTINATION lib ARCHIVE DESTINATION lib RUNTIME DESTINATION bin)
install(FILES "${NTT_MODULE_DIR}/op_host/ntt_internal.h" DESTINATION include/cann_crypto/ntt)
install(FILES "${NTT_MODULE_DIR}/op_kernel/ntt_tiling_data.h" DESTINATION include/cann_crypto/ntt/op_kernel)
install(DIRECTORY "${NTT_MODULE_DIR}/scripts/"
    DESTINATION share/cann_crypto_ntt/ntt_data FILES_MATCHING PATTERN "*.py")
install(FILES "${NTT_MODULE_DIR}/LICENSE" DESTINATION share/licenses/cann_crypto_ntt)
set(CPACK_GENERATOR TGZ)
set(CPACK_PACKAGE_NAME cann_crypto_ntt)
set(CPACK_PACKAGE_FILE_NAME "cann_crypto_ntt-${SOC_VERSION}-${RUN_MODE}")
# run.sh builds first. CPack must not rebuild SDK intermediates rewritten in place.
set(CMAKE_SKIP_INSTALL_ALL_DEPENDENCY TRUE)
include(CPack)
