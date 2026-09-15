# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

include_guard(DIRECTORY)
set(CANN_OPERATOR_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")
include("${CANN_OPERATOR_CMAKE_DIR}/CannFramework.cmake")
option(CANN_BUILD_DEVICE "Build the Ascend C operator package" OFF)
option(CANN_BUILD_TESTS "Build operator tests" ON)
option(CANN_BUILD_CPU_DEBUG_TESTS "Build Kernel UT with GTest and tikicpulib" OFF)
set(CANN_CPU_DEBUG_SOC "Ascend910B1" CACHE STRING "Full SoC name used by tikicpulib")
set(CANN_KERNEL_UT_TIMEOUT 1800 CACHE STRING "Timeout in seconds for each Kernel UT executable")
set(CANN_BUILD_JOBS 8 CACHE STRING "Parallel operator compilation jobs")
set(CANN_DEVICE_ID 0 CACHE STRING "ACL logical device for tests")
set(ASCEND_COMPUTE_UNIT ascend910b CACHE STRING "Target compute unit")
set_property(CACHE ASCEND_COMPUTE_UNIT PROPERTY STRINGS ascend910b ascend910_93)
set(ASCEND_CANN_PACKAGE_PATH "$ENV{ASCEND_HOME_PATH}" CACHE PATH "CANN installation directory")
find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
if(CANN_BUILD_CPU_DEBUG_TESTS AND NOT CANN_BUILD_TESTS)
    message(FATAL_ERROR "CANN_BUILD_CPU_DEBUG_TESTS requires CANN_BUILD_TESTS=ON")
endif()

function(cann_check_gtest_abi)
    get_property(checked GLOBAL PROPERTY CANN_GTEST_ABI_CHECKED)
    if(checked)
        return()
    endif()
    include(CheckCXXSourceCompiles)
    include(CMakePushCheckState)
    cmake_push_check_state(RESET)
    set(CMAKE_REQUIRED_DEFINITIONS -D_GLIBCXX_USE_CXX11_ABI=0)
    set(CMAKE_REQUIRED_LIBRARIES GTest::GTest)
    set(CMAKE_CXX_STANDARD 17)
    # Force a link check, including when the parent toolchain uses compile-only probes.
    set(CMAKE_TRY_COMPILE_TARGET_TYPE EXECUTABLE)
    # Recheck the selected dependency on every configure, not only on the first build.
    unset(CANN_GTEST_ABI_COMPATIBLE CACHE)
    check_cxx_source_compiles([=[
        #include <gtest/gtest.h>
        #include <string>
        #if !defined(_GLIBCXX_USE_CXX11_ABI) || _GLIBCXX_USE_CXX11_ABI != 0
        #error Kernel UT requires _GLIBCXX_USE_CXX11_ABI=0
        #endif
        int main()
        {
            testing::Message message;
            message << std::string("ABI");
            EXPECT_EQ(std::string("ABI"), message.GetString());
            return 0;
        }
    ]=] CANN_GTEST_ABI_COMPATIBLE)
    cmake_pop_check_state()
    if(NOT CANN_GTEST_ABI_COMPATIBLE)
        message(FATAL_ERROR
            "GTest cannot be compiled and linked with _GLIBCXX_USE_CXX11_ABI=0 for Kernel UT. "
            "Build GTest with the same compiler and ABI=0, then set GTest_DIR to its CMake package directory. "
            "See the CMake configure log for the failing compile/link command.")
    endif()
    set_property(GLOBAL PROPERTY CANN_GTEST_ABI_CHECKED TRUE)
endfunction()

function(cann_kernel_unit_test name)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "tikicpulib Kernel UT requires Linux and the CANN CPU debugging library")
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
    cann_check_gtest_abi()
    add_executable(${name} ${ARGN})
    target_compile_features(${name} PRIVATE cxx_std_17)
    target_compile_definitions(${name} PRIVATE _GLIBCXX_USE_CXX11_ABI=0 CANN_CRYPTO_KERNEL_UT=1)
    target_include_directories(${name} PRIVATE "${ASCEND_CANN_PACKAGE_PATH}/include")
    target_link_directories(${name} PRIVATE "${ASCEND_CANN_PACKAGE_PATH}/lib64")
    target_link_libraries(${name} PRIVATE GTest::Main tikicpulib::${CANN_CPU_DEBUG_SOC}
        ascendcl cann_crypto_keccak_reference)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES LABELS kernel_ut TIMEOUT ${CANN_KERNEL_UT_TIMEOUT})
endfunction()

function(cann_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
    endif()
endfunction()

function(cann_add_package name)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "Device compilation requires Linux and the CANN toolkit")
    endif()
    if(NOT ASCEND_COMPUTE_UNIT MATCHES "^(ascend910b|ascend910_93)$")
        message(FATAL_ERROR "Supported compute units: ascend910b (A2), ascend910_93 (A3)")
    endif()
    if(NOT CANN_BUILD_JOBS MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR "CANN_BUILD_JOBS must be a positive integer")
    endif()
    if(NOT CANN_DEVICE_ID MATCHES "^[0-9]+$")
        message(FATAL_ERROR "CANN_DEVICE_ID must be nonnegative")
    endif()
    if(NOT EXISTS "${ASCEND_CANN_PACKAGE_PATH}/include/acl/acl.h")
        message(FATAL_ERROR "Set ASCEND_CANN_PACKAGE_PATH to the CANN directory containing include/acl/acl.h")
    endif()
    cann_find_framework()
    message(STATUS "CANN operator framework: ${CANN_CMAKE_DIR}")
    # 在生成工程前检查编译器的 Python 环境，避免构建到 OPC 阶段才发现缺少依赖。
    execute_process(COMMAND "${Python3_EXECUTABLE}" -c "import numpy; import tbe"
        RESULT_VARIABLE python_status OUTPUT_VARIABLE python_output ERROR_VARIABLE python_error)
    if(NOT python_status EQUAL 0)
        message(FATAL_ERROR "CANN Python dependencies are unavailable for ${Python3_EXECUTABLE}. Load set_env.sh and install the dependencies for this toolkit.\n${python_error}")
    endif()
    include(ExternalProject)
    set(stage "${CMAKE_CURRENT_BINARY_DIR}/ascend_project")
    set(opp "${CMAKE_CURRENT_BINARY_DIR}/opp")
    set(package "${opp}/packages/vendors/${name}")
    set(stage_args)
    foreach(module IN LISTS ARGN)
        list(APPEND stage_args --module "${module}")
        file(GLOB_RECURSE module_sources CONFIGURE_DEPENDS
            "${module}/op_host/*.cpp" "${module}/op_host/*.h"
            "${module}/op_kernel/*.cpp" "${module}/op_kernel/*.h"
            "${module}/*_internal.h" "${module}/*_launch.cpp")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${module_sources})
    endforeach()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CANN_OPERATOR_CMAKE_DIR}/device/CMakeLists.txt"
        "${CANN_OPERATOR_CMAKE_DIR}/CannFramework.cmake"
        "${CANN_OPERATOR_CMAKE_DIR}/stage.py" "${CANN_OPERATOR_CMAKE_DIR}/verify_package.py")
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${CANN_OPERATOR_CMAKE_DIR}/stage.py"
        --output "${stage}" ${stage_args} RESULT_VARIABLE stage_result)
    if(NOT stage_result EQUAL 0)
        message(FATAL_ERROR "Cannot prepare the Ascend C build project")
    endif()
    ExternalProject_Add(${name}_opp
        SOURCE_DIR "${stage}" BINARY_DIR "${opp}"
        DOWNLOAD_COMMAND "" UPDATE_COMMAND ""
        CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release
            -Dvendor_name=${name}
            -DCANN_CMAKE_DIR=${CANN_CMAKE_DIR}
            -DASCEND_CANN_PACKAGE_PATH=${ASCEND_CANN_PACKAGE_PATH}
            -DASCEND_COMPUTE_UNIT=${ASCEND_COMPUTE_UNIT}
            -DASCEND_PYTHON_EXECUTABLE=${Python3_EXECUTABLE}
            -DENABLE_SOURCE_PACKAGE=ON -DENABLE_BINARY_PACKAGE=ON
            -DENABLE_CROSS_COMPILE=OFF -DASCEND_PACK_SHARED_LIBRARY=OFF
            -DCMAKE_INSTALL_PREFIX=${opp}
        BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --target binary --parallel ${CANN_BUILD_JOBS}
        INSTALL_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --target install
            COMMAND "${Python3_EXECUTABLE}" "${CANN_OPERATOR_CMAKE_DIR}/verify_package.py" "${package}" --module ${name}
        BUILD_ALWAYS TRUE
        BUILD_BYPRODUCTS "${package}/op_api/lib/libcust_opapi.so")
    # 接口头由 opbuild 生成；依赖算子包完成后再编译调用程序。
    file(MAKE_DIRECTORY "${package}/op_api/include")
    add_library(${name}_runtime INTERFACE)
    add_dependencies(${name}_runtime ${name}_opp)
    target_compile_features(${name}_runtime INTERFACE cxx_std_17)
    target_include_directories(${name}_runtime INTERFACE
        "${ASCEND_CANN_PACKAGE_PATH}/include" "${package}/op_api/include")
    target_link_directories(${name}_runtime INTERFACE "${ASCEND_CANN_PACKAGE_PATH}/lib64")
    target_link_libraries(${name}_runtime INTERFACE
        "${package}/op_api/lib/libcust_opapi.so" ascendcl nnopbase)
    set(${name}_PACKAGE "${package}" PARENT_SCOPE)
endfunction()

function(cann_device_test name runtime)
    set(test_name "${name}")
    if(ARGC GREATER 2)
        string(JOIN "_" suffix ${ARGN})
        set(test_name "${name}_${suffix}")
    endif()
    add_test(NAME ${test_name} COMMAND "${CMAKE_COMMAND}" -E env
        "ASCEND_CUSTOM_OPP_PATH=${${runtime}_PACKAGE}"
        "CANN_CRYPTO_SOC_TARGET=${ASCEND_COMPUTE_UNIT}"
        "LD_LIBRARY_PATH=${${runtime}_PACKAGE}/op_api/lib:${ASCEND_CANN_PACKAGE_PATH}/lib64:$ENV{LD_LIBRARY_PATH}"
        $<TARGET_FILE:${name}> ${CANN_DEVICE_ID} ${ARGN})
    set_tests_properties(${test_name} PROPERTIES LABELS device RESOURCE_LOCK ascend_device TIMEOUT 600)
endfunction()
