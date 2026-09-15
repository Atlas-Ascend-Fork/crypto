# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.16)
project(opp LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_COMPILE "${CMAKE_CXX_COMPILER}")

foreach(module config.cmake intf.cmake func.cmake)
    if(NOT EXISTS "${CANN_CMAKE_DIR}/${module}")
        message(FATAL_ERROR "Missing installed CANN framework module: ${CANN_CMAKE_DIR}/${module}")
    endif()
    include("${CANN_CMAKE_DIR}/${module}")
endforeach()
foreach(api npu_op_package npu_op_code_gen npu_op_library
            npu_op_kernel_sources npu_op_kernel_library npu_op_package_add)
    if(NOT COMMAND ${api})
        message(FATAL_ERROR "The selected CANN framework does not provide ${api}")
    endif()
endforeach()
message(STATUS "CANN operator framework: ${CANN_CMAKE_DIR}")

set(package_name "${vendor_name}")
npu_op_package(${package_name} TYPE RUN CONFIG INSTALL_PATH "${CMAKE_INSTALL_PREFIX}")
file(MAKE_DIRECTORY "${ASCEND_AUTOGEN_PATH}")
aux_source_directory("${CMAKE_CURRENT_SOURCE_DIR}/op_host" ops_srcs)
npu_op_code_gen(SRC ${ops_srcs} PACKAGE ${package_name}
    OUT_DIR "${ASCEND_AUTOGEN_PATH}"
    COMPILE_OPTIONS -std=c++17
        "-I${CMAKE_CURRENT_SOURCE_DIR}/op_host"
        "-I${CMAKE_CURRENT_SOURCE_DIR}/op_kernel")

file(GLOB aclnn_src "${ASCEND_AUTOGEN_PATH}/aclnn_*.cpp")
file(GLOB aclnn_headers "${ASCEND_AUTOGEN_PATH}/aclnn_*.h")
file(GLOB proto_src "${ASCEND_AUTOGEN_PATH}/op_proto.cc"
    "${ASCEND_AUTOGEN_PATH}/group_proto/*.cc"
    "${ASCEND_AUTOGEN_PATH}/group_op_proto/*.cc")
file(GLOB proto_headers "${ASCEND_AUTOGEN_PATH}/op_proto.h"
    "${ASCEND_AUTOGEN_PATH}/group_proto/*.h"
    "${ASCEND_AUTOGEN_PATH}/group_op_proto/*.h")
file(GLOB fallback_src "${ASCEND_AUTOGEN_PATH}/fallback_*.cpp")
if(NOT aclnn_src OR NOT aclnn_headers OR NOT EXISTS "${ASCEND_AUTOGEN_PATH}/op_proto.cc")
    message(FATAL_ERROR "CANN op_build did not generate the ACLNN and operator prototype sources")
endif()
set_source_files_properties(${aclnn_src} ${proto_src} ${fallback_src} PROPERTIES GENERATED TRUE)
npu_op_library(cust_opapi ACLNN ${aclnn_src})
npu_op_library(cust_optiling TILING ${ops_srcs} ${fallback_src})
npu_op_library(cust_op_proto GRAPH ${ops_srcs} ${proto_src})
foreach(target cust_opapi cust_optiling cust_op_proto)
    target_compile_features(${target} PRIVATE cxx_std_17)
    target_include_directories(${target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/op_host" "${CMAKE_CURRENT_SOURCE_DIR}/op_kernel")
    target_compile_options(${target} PRIVATE -fvisibility=hidden)
endforeach()
set_target_properties(cust_opapi PROPERTIES OUTPUT_NAME cust_opapi)

# Recreate source mappings on every configuration to avoid duplicate entries.
file(WRITE "${ASCEND_AUTOGEN_PATH}/ascendc_kernels_custom_source_files.ini" "")
npu_op_kernel_sources(ascendc_kernels OP_TYPE KeccakF1600Tensor
    KERNEL_DIR . KERNEL_FILE keccak_f1600_tensor.cpp)
foreach(bits 128 256)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/op_kernel/shake${bits}_tensor.cpp")
        npu_op_kernel_sources(ascendc_kernels OP_TYPE Shake${bits}Tensor
            KERNEL_DIR . KERNEL_FILE shake${bits}_tensor.cpp)
    endif()
endforeach()
npu_op_kernel_library(ascendc_kernels
    SRC_BASE "${CMAKE_CURRENT_SOURCE_DIR}/op_kernel" TILING_LIBRARY cust_optiling)
npu_op_package_add(${package_name} LIBRARY cust_opapi cust_optiling cust_op_proto ascendc_kernels)
npu_op_package_add(${package_name} TYPE ACLNN FILES ${aclnn_headers})
if(proto_headers)
    npu_op_package_add(${package_name} TYPE GRAPH FILES ${proto_headers})
endif()
