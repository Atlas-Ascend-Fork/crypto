# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# Licensed under CANN Open Software License Agreement Version 2.0. See ../LICENSE.
# Only the installed CANN SDK and this repository are required.
get_filename_component(NTT_MODULE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(ASCEND_COMPUTE_UNIT ascend910b CACHE STRING "Target CANN compute unit")
set(CRYPTO_NTT_VENDOR cann_crypto CACHE STRING "Private NTT operator vendor")
set(ASCEND_PYTHON_EXECUTABLE python3 CACHE STRING "Python interpreter for CANN tools")
if(NOT EXISTS "${ASCEND_CANN_PACKAGE_PATH}/include/register/op_def_registry.h")
    message(FATAL_ERROR "Set ASCEND_CANN_PACKAGE_PATH to the installed CANN SDK")
endif()
find_path(NTT_CANN_CMAKE_DIR config.cmake
    PATHS "${ASCEND_CANN_PACKAGE_PATH}/compiler/tikcpp/ascendc_kernel_cmake/fwk_modules"
          "${ASCEND_CANN_PACKAGE_PATH}/tools/tikcpp/ascendc_kernel_cmake/fwk_modules"
    NO_DEFAULT_PATH REQUIRED)
include("${NTT_CANN_CMAKE_DIR}/config.cmake")
include("${NTT_CANN_CMAKE_DIR}/intf.cmake")
include("${NTT_CANN_CMAKE_DIR}/func.cmake")
set(NTT_AUTOGEN "${CMAKE_BINARY_DIR}/autogen")
file(MAKE_DIRECTORY "${NTT_AUTOGEN}")

npu_op_package(cann_crypto_ntt TYPE RUN CONFIG
    VENDOR_NAME "${CRYPTO_NTT_VENDOR}"
    INSTALL_PATH "${CMAKE_BINARY_DIR}/package"
    ENABLE_DEFAULT_PACKAGE_NAME_RULE FALSE)
# CANN 8.5 moved version manifests out of toolkit/. Correct the SDK template's
# generated package metadata using the installed compiler manifest.
add_custom_command(TARGET gen_version_info POST_BUILD
    COMMAND "${CMAKE_COMMAND}" "-DNTT_SDK_ROOT=${ASCEND_CANN_PACKAGE_PATH}"
        "-DNTT_VERSION_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/version.info"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/write_version.cmake")
npu_op_code_gen(PACKAGE cann_crypto_ntt
    SRC "${NTT_MODULE_DIR}/adapters/cann/ntt_def.cpp"
    OUT_DIR "${NTT_AUTOGEN}"
    COMPILE_OPTIONS -std=c++17
    OPTIONS OPS_PROJECT_NAME aclnn OPS_ENABLE_SOURCE_PACKAGE True)
file(GLOB NTT_ACLNN_SRC "${NTT_AUTOGEN}/aclnn_*.cpp")
file(GLOB NTT_ACLNN_HEADERS "${NTT_AUTOGEN}/aclnn_*.h")
if(NOT NTT_ACLNN_SRC OR NOT EXISTS "${NTT_AUTOGEN}/op_proto.cc")
    message(FATAL_ERROR "CANN op_build did not generate NTT ACLNN/proto sources")
endif()
npu_op_library(crypto_ntt_aclnn ACLNN ${NTT_ACLNN_SRC})
npu_op_library(crypto_ntt_tiling TILING
    "${NTT_MODULE_DIR}/adapters/cann/ntt_tiling.cpp"
    "${NTT_MODULE_DIR}/op_host/ntt_tiling.cpp"
    "${NTT_MODULE_DIR}/ntt_launch.cpp")
npu_op_library(crypto_ntt_proto GRAPH
    "${NTT_AUTOGEN}/op_proto.cc"
    "${NTT_MODULE_DIR}/adapters/cann/ntt_infershape.cpp"
    "${NTT_MODULE_DIR}/adapters/cann/op_graph/ntt_graph_infer.cpp")
foreach(ntt_target crypto_ntt_aclnn crypto_ntt_tiling crypto_ntt_proto)
    target_compile_features(${ntt_target} PRIVATE cxx_std_17)
endforeach()
file(WRITE "${NTT_AUTOGEN}/crypto_ntt_kernels_custom_source_files.ini" "")
npu_op_kernel_sources(crypto_ntt_kernels OP_TYPE Ntt
    KERNEL_FILE ntt.cpp
    KERNEL_DIR .)
npu_op_kernel_library(crypto_ntt_kernels
    SRC_BASE "${NTT_MODULE_DIR}/op_kernel" TILING_LIBRARY crypto_ntt_tiling)
npu_op_package_add(cann_crypto_ntt LIBRARY
    crypto_ntt_aclnn crypto_ntt_tiling crypto_ntt_proto crypto_ntt_kernels)
npu_op_package_add(cann_crypto_ntt TYPE ACLNN FILES ${NTT_ACLNN_HEADERS})

add_executable(ntt_aclnn_example "${NTT_MODULE_DIR}/examples/test_aclnn_ntt.cpp")
add_dependencies(ntt_aclnn_example binary)
set_target_properties(ntt_aclnn_example PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
target_compile_features(ntt_aclnn_example PRIVATE cxx_std_17)
target_include_directories(ntt_aclnn_example PRIVATE "${NTT_AUTOGEN}")
target_compile_definitions(ntt_aclnn_example PRIVATE
    NTT_DATA_SOURCE_DIR="${NTT_MODULE_DIR}/tests/ut/op_kernel/ntt_data")
target_link_libraries(ntt_aclnn_example PRIVATE cann_crypto_ntt_ascendc_cust_opapi intf_pub ascendcl nnopbase)
if(CRYPTO_NTT_SIMULATOR)
    set(NTT_SIM_LIB "${ASCEND_CANN_PACKAGE_PATH}/tools/simulator/Ascend910B1/lib")
    if(NOT EXISTS "${NTT_SIM_LIB}/libruntime_camodel.so")
        message(FATAL_ERROR "Ascend910B1 Camodel is missing from the CANN SDK")
    endif()
    set(NTT_SIM_COMPAT "${CMAKE_BINARY_DIR}/simulator_compat")
    file(MAKE_DIRECTORY "${NTT_SIM_COMPAT}")
    file(CREATE_LINK "${NTT_SIM_LIB}/libruntime_camodel.so" "${NTT_SIM_COMPAT}/libruntime.so" SYMBOLIC)
    file(CREATE_LINK "${NTT_SIM_LIB}/libnpu_drv_camodel.so" "${NTT_SIM_COMPAT}/libascend_hal.so" SYMBOLIC)
    target_link_directories(ntt_aclnn_example PRIVATE "${NTT_SIM_COMPAT}" "${NTT_SIM_LIB}")
    target_link_libraries(ntt_aclnn_example PRIVATE runtime_camodel npu_drv_camodel)
    target_link_options(ntt_aclnn_example PRIVATE -Wl,--allow-shlib-undefined)
endif()
