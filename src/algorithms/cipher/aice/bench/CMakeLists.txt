# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# Licensed under CANN Open Software License Agreement Version 2.0; see LICENSE.
if(CANN_BUILD_DEVICE)
    add_executable(aice_bench aice_bench.cpp)
    target_compile_definitions(aice_bench PRIVATE CANN_DEVICE_ID=${CANN_DEVICE_ID})
    target_include_directories(aice_bench PRIVATE "${ASCEND_CANN_PACKAGE_PATH}/include")
    target_link_directories(aice_bench PRIVATE "${ASCEND_CANN_PACKAGE_PATH}/lib64")
    target_link_libraries(aice_bench PRIVATE cann_crypto_aice aice_cpu ascendcl)
    set_target_properties(aice_bench PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bench)
endif()
