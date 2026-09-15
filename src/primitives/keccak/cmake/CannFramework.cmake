# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

include_guard(GLOBAL)

function(cann_find_framework)
    set(search_paths)
    foreach(sdk_root
            "${ASCEND_CANN_PACKAGE_PATH}"
            "${ASCEND_CANN_PACKAGE_PATH}/toolkit"
            "${ASCEND_CANN_PACKAGE_PATH}/${CMAKE_SYSTEM_PROCESSOR}-linux")
        foreach(tool_dir compiler tools)
            list(APPEND search_paths
                "${sdk_root}/${tool_dir}/tikcpp/ascendc_kernel_cmake/fwk_modules")
        endforeach()
    endforeach()
    find_path(CANN_CMAKE_DIR config.cmake PATHS ${search_paths} NO_DEFAULT_PATH
        DOC "Installed CANN operator CMake framework directory")
    foreach(module config.cmake intf.cmake func.cmake)
        if(NOT EXISTS "${CANN_CMAKE_DIR}/${module}")
            message(FATAL_ERROR
                "CANN operator CMake framework is unavailable (${module}). "
                "Set ASCEND_CANN_PACKAGE_PATH to the toolkit installation, or "
                "set CANN_CMAKE_DIR to its ascendc_kernel_cmake/fwk_modules directory. "
                "Searched: ${search_paths}")
        endif()
    endforeach()
    set(CANN_CMAKE_DIR "${CANN_CMAKE_DIR}" PARENT_SCOPE)
endfunction()
