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
project(AscendOps VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
include(cmake/python.cmake)
include(cmake/torch.cmake)
include(cmake/torch_npu.cmake)
set(CANN_BUILD_DEVICE ON CACHE BOOL "Build AES for the Torch adapter" FORCE)
set(CANN_BUILD_TESTS OFF CACHE BOOL "Core tests are run from the standalone AES build" FORCE)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../src/primitives/AES" aes_core)
add_library(_C SHARED csrc/extension.cpp csrc/aes_binding.cpp)
set_target_properties(_C PROPERTIES PREFIX "" SUFFIX ".abi3.so" OUTPUT_NAME "_C"
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN;${TORCH_INSTALL_PREFIX}/lib;${TORCH_NPU_LIB_PATH}")
target_compile_definitions(_C PRIVATE Py_LIMITED_API=0x03080000 EXTENSION_MODULE_NAME=ascend_ops)
separate_arguments(TORCH_COMPILE_OPTIONS NATIVE_COMMAND "${TORCH_CXX_FLAGS}")
target_compile_options(_C PRIVATE ${TORCH_COMPILE_OPTIONS})
target_include_directories(_C PRIVATE ${Python3_INCLUDE_DIRS} ${TORCH_INCLUDE_DIRS} ${TORCH_NPU_INCLUDE_PATH})
target_link_directories(_C PRIVATE ${TORCH_NPU_LIB_PATH})
target_link_libraries(_C PRIVATE ${TORCH_LIBRARIES} torch_npu cann_crypto_aes)
add_custom_command(TARGET _C POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:_C> "${CMAKE_CURRENT_SOURCE_DIR}/ascend_ops/"
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:cann_crypto_aes> "${CMAKE_CURRENT_SOURCE_DIR}/ascend_ops/"
    COMMENT "Bundling the Torch extension and the shared AES core in ascend_ops")
