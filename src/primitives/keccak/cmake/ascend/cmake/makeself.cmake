set(MAKESELF_DIR "$ENV{SHA3_MAKESELF_DIR}")
if("${MAKESELF_DIR}" STREQUAL "" AND
   EXISTS "${CMAKE_CURRENT_LIST_DIR}/util/makeself/makeself.sh")
  set(MAKESELF_DIR "${CMAKE_CURRENT_LIST_DIR}/util/makeself")
endif()

if(NOT EXISTS "${MAKESELF_DIR}/makeself.sh" OR
   NOT EXISTS "${MAKESELF_DIR}/makeself-header.sh")
  message(FATAL_ERROR
    "makeself is unavailable. Use 'bash build.sh' for a directly usable "
    "local OPP tree, or set SHA3_MAKESELF_DIR before requesting package.")
endif()

execute_process(COMMAND bash ${MAKESELF_DIR}/makeself.sh
                        --header ${MAKESELF_DIR}/makeself-header.sh
                        --help-header ./help.info --tar-format posix
                        --gzip --complevel 4 --nomd5 --sha256
                        ./ ${CPACK_PACKAGE_FILE_NAME} "version:1.0" ./install.sh
                WORKING_DIRECTORY ${CPACK_TEMPORARY_DIRECTORY}
                RESULT_VARIABLE EXEC_RESULT
                ERROR_VARIABLE  EXEC_ERROR
)

if (NOT "${EXEC_RESULT}x" STREQUAL "0x")
  message(FATAL_ERROR "CPack Command error: ${EXEC_RESULT}\n${EXEC_ERROR}")
endif()

execute_process(COMMAND cp ${CPACK_EXTERNAL_BUILT_PACKAGES} ${CPACK_PACKAGE_DIRECTORY}/
        COMMAND echo "Copy ${CPACK_EXTERNAL_BUILT_PACKAGES} to ${CPACK_PACKAGE_DIRECTORY}/"
        WORKING_DIRECTORY ${CPACK_TEMPORARY_DIRECTORY}
    )

if (NOT "${CPACK_PACKAGE_DIRECTORY}x" STREQUAL "${CPACK_INSTALL_PREFIX}x")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CPACK_INSTALL_PREFIX}
        WORKING_DIRECTORY ${CPACK_TEMPORARY_DIRECTORY}
    )

    execute_process(
        COMMAND cp ${CPACK_EXTERNAL_BUILT_PACKAGES} ${CPACK_INSTALL_PREFIX}/
        COMMAND echo "Copy ${CPACK_EXTERNAL_BUILT_PACKAGES} to ${CPACK_INSTALL_PREFIX}/"
        WORKING_DIRECTORY ${CPACK_TEMPORARY_DIRECTORY}
    )
endif()
