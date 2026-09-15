# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

foreach(version_file
    "${NTT_SDK_ROOT}/share/info/bisheng-compiler/version.info"
    "${NTT_SDK_ROOT}/toolkit/version.info")
    if(EXISTS "${version_file}")
        file(STRINGS "${version_file}" version_line REGEX "^Version=")
        if(version_line)
            list(GET version_line 0 version_line)
            string(REGEX REPLACE "^Version=" "" sdk_version "${version_line}")
            file(WRITE "${NTT_VERSION_OUTPUT}" "custom_opp_compiler_version=${sdk_version}\n")
            return()
        endif()
    endif()
endforeach()
message(FATAL_ERROR "Cannot determine the installed CANN compiler version")
