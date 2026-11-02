# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# Licensed under CANN Open Software License Agreement Version 2.0; see LICENSE.

# Host API validation and C header compatibility.
add_executable(test_aice_host ut/op_host/test_aice_host.cpp ../op_host/aice_host.cpp)
target_include_directories(test_aice_host PRIVATE ut/op_host/stubs ${AICE_REPOSITORY_ROOT}/include)
target_compile_options(test_aice_host PRIVATE -Wall -Wextra -Werror)
add_test(NAME aice_host COMMAND test_aice_host)

add_library(aice_public_header OBJECT ut/op_host/test_public_header.c)
target_include_directories(aice_public_header PRIVATE ${AICE_REPOSITORY_ROOT}/include)
target_compile_options(aice_public_header PRIVATE -Wall -Wextra -Werror)

# CPU implementation tests and fixed known-answer tests.
add_executable(test_aice_cpu ut/op_host/test_aice_cpu.c)
target_link_libraries(test_aice_cpu PRIVATE aice_cpu)
add_test(NAME aice_cpu COMMAND test_aice_cpu)

add_executable(test_aice_kat ut/op_host/test_aice_kat.cpp)
target_compile_definitions(test_aice_kat PRIVATE AICE_KAT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/assets/aice_kat.rsp")
target_link_libraries(test_aice_kat PRIVATE aice_cpu)
add_test(NAME aice_cpu_kat COMMAND test_aice_kat)
set_tests_properties(aice_cpu_kat PROPERTIES LABELS "op_host;kat")

# Host-only build regression and Host test labels.
add_test(NAME aice_host_only_build COMMAND ${CMAKE_COMMAND}
    -DAICE_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/..
    -DAICE_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/host-only-regression
    -P ${CMAKE_CURRENT_SOURCE_DIR}/ut/op_host/test_host_only_build.cmake)
set_tests_properties(aice_host aice_cpu aice_host_only_build PROPERTIES LABELS "op_host")

if(CANN_BUILD_DEVICE)
    # Production shared-library API tests (executed on Host).
    add_executable(test_aice_shared_launch ut/op_host/test_shared_launch.cpp)
    target_include_directories(test_aice_shared_launch PRIVATE ${ASCEND_CANN_PACKAGE_PATH}/include)
    target_link_libraries(test_aice_shared_launch PRIVATE cann_crypto_aice)
    add_test(NAME aice_shared_launch COMMAND test_aice_shared_launch)
    set_tests_properties(aice_shared_launch PROPERTIES LABELS "op_host")

    # Device ST: self-test, CPU cross-test, and tail regression.
    add_executable(test_aice_device st/test_aice_device.cpp)
    target_compile_definitions(test_aice_device PRIVATE CANN_DEVICE_ID=${CANN_DEVICE_ID})
    target_include_directories(test_aice_device PRIVATE ${ASCEND_CANN_PACKAGE_PATH}/include)
    target_link_directories(test_aice_device PRIVATE ${ASCEND_CANN_PACKAGE_PATH}/lib64)
    target_link_libraries(test_aice_device PRIVATE cann_crypto_aice aice_cpu ascendcl)
    set_target_properties(test_aice_device PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    add_test(NAME aice_device COMMAND test_aice_device --self-test)
    add_test(NAME aice_cross COMMAND test_aice_device --cross-test)
    add_test(NAME aice_tail_regression COMMAND test_aice_device --tail-regression)

    # Device known-answer tests using the shared KAT fixture.
    add_executable(test_aice_device_kat ut/op_host/test_aice_kat.cpp)
    target_compile_definitions(test_aice_device_kat PRIVATE AICE_KAT_DEVICE CANN_DEVICE_ID=${CANN_DEVICE_ID}
        AICE_KAT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/assets/aice_kat.rsp")
    target_include_directories(test_aice_device_kat PRIVATE ${ASCEND_CANN_PACKAGE_PATH}/include)
    target_link_directories(test_aice_device_kat PRIVATE ${ASCEND_CANN_PACKAGE_PATH}/lib64)
    target_link_libraries(test_aice_device_kat PRIVATE cann_crypto_aice aice_cpu ascendcl)
    set_target_properties(test_aice_device_kat PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    add_test(NAME aice_device_kat COMMAND test_aice_device_kat)

    # Serialize device tests; build.sh supplies the configurable CTest timeout.
    set_tests_properties(aice_device aice_cross aice_tail_regression aice_device_kat PROPERTIES
        LABELS "device;st" RESOURCE_LOCK ascend_device)
endif()
