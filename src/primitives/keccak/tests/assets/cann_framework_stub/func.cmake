# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

function(record_call name)
    file(APPEND "${CMAKE_BINARY_DIR}/framework_calls.txt" "${name};${ARGN}\n")
endfunction()

function(npu_op_package name)
    cmake_parse_arguments(PKG "CONFIG" "TYPE;INSTALL_PATH" "" ${ARGN})
    if(NOT PKG_TYPE STREQUAL "RUN" OR NOT name STREQUAL vendor_name
            OR NOT PKG_INSTALL_PATH STREQUAL CMAKE_INSTALL_PREFIX)
        message(FATAL_ERROR "Invalid package configuration")
    endif()
    record_call(package ${name} ${ARGN})
endfunction()

function(npu_op_code_gen)
    cmake_parse_arguments(GEN "" "PACKAGE;OUT_DIR" "SRC;COMPILE_OPTIONS" ${ARGN})
    record_call(code_gen ${ARGN} PYTHON "${ASCEND_PYTHON_EXECUTABLE}")
    check_codegen_headers("${GEN_OUT_DIR}" ${GEN_COMPILE_OPTIONS})
    foreach(source IN LISTS GEN_SRC)
        if(source MATCHES "/([a-z0-9_]+)_def[.]cpp$")
            set(stem "${CMAKE_MATCH_1}")
            file(WRITE "${GEN_OUT_DIR}/aclnn_${stem}.cpp" "int generated_${stem}() { return 0; }\n")
            file(WRITE "${GEN_OUT_DIR}/aclnn_${stem}.h" "#pragma once\n")
        endif()
    endforeach()
    file(WRITE "${GEN_OUT_DIR}/op_proto.cc" "int generated_proto() { return 0; }\n")
    file(WRITE "${GEN_OUT_DIR}/op_proto.h" "#pragma once\n")
    file(WRITE "${GEN_OUT_DIR}/fallback_test.cpp" "int generated_fallback() { return 0; }\n")
endfunction()

function(check_codegen_headers output_dir)
    file(GLOB headers "${CMAKE_SOURCE_DIR}/op_kernel/*_tiling_data.h"
        "${CMAKE_SOURCE_DIR}/op_kernel/*_tiling_key.h")
    set(probe "")
    foreach(header IN LISTS headers)
        get_filename_component(name "${header}" NAME)
        string(APPEND probe "#include \"${name}\"\n")
    endforeach()
    if(EXISTS "${CMAKE_SOURCE_DIR}/op_host/shake_internal.h")
        string(APPEND probe "#include \"shake_internal.h\"\n")
    endif()
    file(WRITE "${output_dir}/header_probe.cpp" "${probe}")
    if(MSVC)
        set(options /std:c++17 /c)
        foreach(option IN LISTS ARGN)
            if(option MATCHES "^-I(.+)$")
                list(APPEND options "/I${CMAKE_MATCH_1}")
            endif()
        endforeach()
        set(object_option "/Fo${output_dir}/header_probe.obj")
    else()
        set(options ${ARGN} -c)
        set(object_option -o "${output_dir}/header_probe.o")
    endif()
    execute_process(COMMAND "${CMAKE_CXX_COMPILER}" ${options}
        "${output_dir}/header_probe.cpp" ${object_option}
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "Code generation header probe failed: ${output}\n${error}")
    endif()
endfunction()

function(npu_op_library name type)
    record_call(library ${name} ${type} ${ARGN})
    add_library(${name} SHARED ${ARGN})
endfunction()

function(npu_op_kernel_sources name)
    cmake_parse_arguments(KERNEL "" "OP_TYPE;KERNEL_DIR;KERNEL_FILE" "" ${ARGN})
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/op_kernel/${KERNEL_KERNEL_DIR}/${KERNEL_KERNEL_FILE}")
        message(FATAL_ERROR "Missing kernel source")
    endif()
    file(APPEND "${ASCEND_AUTOGEN_PATH}/${name}_custom_source_files.ini" "${KERNEL_OP_TYPE}\n")
    record_call(kernel_source ${name} ${ARGN})
endfunction()

function(npu_op_kernel_library name)
    cmake_parse_arguments(KERNEL "" "SRC_BASE;TILING_LIBRARY" "" ${ARGN})
    if(NOT IS_DIRECTORY "${KERNEL_SRC_BASE}" OR NOT TARGET ${KERNEL_TILING_LIBRARY})
        message(FATAL_ERROR "Invalid kernel library configuration")
    endif()
    add_custom_target(${name})
    record_call(kernel_library ${name} ${ARGN})
endfunction()

function(npu_op_package_add name)
    cmake_parse_arguments(PKG "" "TYPE" "LIBRARY;FILES" ${ARGN})
    foreach(target IN LISTS PKG_LIBRARY)
        if(NOT TARGET ${target})
            message(FATAL_ERROR "Missing package target: ${target}")
        endif()
    endforeach()
    record_call(package_add ${name} ${ARGN})
endfunction()
