# ----------------------------------------------------------------------------
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

add_library(cann_crypto_ntt STATIC ntt_launch.cpp op_host/ntt_tiling.cpp)
target_compile_features(cann_crypto_ntt PUBLIC cxx_std_17)
target_include_directories(cann_crypto_ntt PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(cann_crypto_ntt PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(cann_crypto_ntt PRIVATE -Wall -Wextra -Wpedantic -Werror)

if(CRYPTO_NTT_CANN_ADAPTER)
    find_path(NTT_CANN_INCLUDE_DIR register/op_impl_registry.h
        HINTS "${ASCEND_CANN_PACKAGE_PATH}/include" "$ENV{ASCEND_HOME_PATH}/include")
    find_library(NTT_CANN_REGISTER register
        HINTS "${ASCEND_CANN_PACKAGE_PATH}/lib64" "$ENV{ASCEND_HOME_PATH}/lib64")
    find_library(NTT_CANN_GRAPH graph
        HINTS "${ASCEND_CANN_PACKAGE_PATH}/lib64" "$ENV{ASCEND_HOME_PATH}/lib64")
    find_library(NTT_CANN_EXE_GRAPH exe_graph
        HINTS "${ASCEND_CANN_PACKAGE_PATH}/lib64" "$ENV{ASCEND_HOME_PATH}/lib64")
    find_library(NTT_CANN_OPP_REGISTRY opp_registry
        HINTS "${ASCEND_CANN_PACKAGE_PATH}/lib64" "$ENV{ASCEND_HOME_PATH}/lib64")
    if(NOT NTT_CANN_INCLUDE_DIR OR NOT NTT_CANN_REGISTER OR NOT NTT_CANN_GRAPH OR
       NOT NTT_CANN_EXE_GRAPH OR NOT NTT_CANN_OPP_REGISTRY)
        message(FATAL_ERROR "Set ASCEND_CANN_PACKAGE_PATH to a CANN SDK containing headers and host libraries")
    endif()
    add_library(cann_crypto_ntt_cann SHARED
        adapters/cann/ntt_tiling.cpp adapters/cann/ntt_def.cpp
        adapters/cann/ntt_infershape.cpp adapters/cann/op_graph/ntt_graph_infer.cpp)
    target_include_directories(cann_crypto_ntt_cann SYSTEM PRIVATE "${NTT_CANN_INCLUDE_DIR}")
    target_link_libraries(cann_crypto_ntt_cann PRIVATE cann_crypto_ntt "${NTT_CANN_REGISTER}"
        "${NTT_CANN_GRAPH}" "${NTT_CANN_EXE_GRAPH}" "${NTT_CANN_OPP_REGISTRY}")
    target_link_options(cann_crypto_ntt_cann PRIVATE -Wl,-z,defs)
    if(BUILD_TESTING)
        find_library(NTT_CANN_METADEF metadef
            HINTS "${ASCEND_CANN_PACKAGE_PATH}/lib64" "$ENV{ASCEND_HOME_PATH}/lib64")
        find_library(NTT_CANN_C_SEC c_sec
            HINTS "${ASCEND_CANN_PACKAGE_PATH}/lib64" "$ENV{ASCEND_HOME_PATH}/lib64")
        if(NOT NTT_CANN_METADEF OR NOT NTT_CANN_C_SEC)
            message(FATAL_ERROR "CANN adapter tests require libmetadef and libc_sec")
        endif()
        add_executable(ntt_cann_adapter_test tests/host/test_ntt_cann_adapter.cpp)
        target_include_directories(ntt_cann_adapter_test SYSTEM PRIVATE "${NTT_CANN_INCLUDE_DIR}")
        target_link_libraries(ntt_cann_adapter_test PRIVATE cann_crypto_ntt cann_crypto_ntt_cann
            "${NTT_CANN_METADEF}" "${NTT_CANN_EXE_GRAPH}" "${NTT_CANN_C_SEC}")
        get_filename_component(NTT_CANN_LIB_DIR "${NTT_CANN_METADEF}" DIRECTORY)
        add_test(NAME ntt_cann_adapter COMMAND "${CMAKE_COMMAND}" -E env
            "LD_LIBRARY_PATH=${NTT_CANN_LIB_DIR}:$ENV{LD_LIBRARY_PATH}" $<TARGET_FILE:ntt_cann_adapter_test>)
    endif()
endif()

if(BUILD_TESTING)
    add_executable(ntt_host_test tests/host/test_ntt_host.cpp)
    target_link_libraries(ntt_host_test PRIVATE cann_crypto_ntt)
    target_compile_options(ntt_host_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME ntt_host COMMAND ntt_host_test)
endif()

if(CRYPTO_NTT_DEVICE)
    add_subdirectory(device)
endif()
