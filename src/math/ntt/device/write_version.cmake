# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# Licensed under CANN Open Software License Agreement Version 2.0. See ../LICENSE.
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
