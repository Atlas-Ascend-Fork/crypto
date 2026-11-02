# Host builds must not require a supported device SoC or a CANN SDK.
# Build the real Host tests in a separate tree, but do not recursively run CTest.
execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${AICE_SOURCE_DIR}" -B "${AICE_BINARY_DIR}"
        -DCANN_BUILD_DEVICE=OFF -DCANN_BUILD_TESTS=ON
        -DASCEND_COMPUTE_UNIT=ascend910_93 -DASCEND_CANN_PACKAGE_PATH=
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Host-only configuration rejected an unrelated device SoC")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${AICE_BINARY_DIR}"
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Host-only build requires device dependencies")
endif()
