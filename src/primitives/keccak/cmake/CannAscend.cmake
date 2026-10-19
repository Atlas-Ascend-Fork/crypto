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
option(CANN_BUILD_DEVICE "Build direct Ascend C kernel libraries" OFF)
set(ASCEND_CANN_PACKAGE_PATH "$ENV{ASCEND_HOME_PATH}" CACHE PATH "CANN installation directory")
set(ASCEND_COMPUTE_UNIT ascend910b CACHE STRING "Target product: ascend910b (A2) or ascend910_93 (A3)")
set_property(CACHE ASCEND_COMPUTE_UNIT PROPERTY STRINGS ascend910b ascend910_93)

if(CANN_BUILD_DEVICE)
    if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "Device compilation requires Linux and the CANN toolkit")
    endif()
    if(NOT ASCEND_COMPUTE_UNIT MATCHES "^(ascend910b|ascend910_93)$")
        message(FATAL_ERROR "Supported compute units: ascend910b (A2), ascend910_93 (A3)")
    endif()
    if(NOT EXISTS "${ASCEND_CANN_PACKAGE_PATH}/include/acl/acl.h")
        message(FATAL_ERROR "Set ASCEND_CANN_PACKAGE_PATH to the CANN installation directory")
    endif()
    # Both A2 and A3 use the dav-2201 vector architecture.
    set(CMAKE_ASC_ARCHITECTURES dav-2201 CACHE STRING "NPU architecture")
    if(NOT CMAKE_ASC_ARCHITECTURES STREQUAL "dav-2201")
        message(FATAL_ERROR "Keccak and SHAKE currently require CMAKE_ASC_ARCHITECTURES=dav-2201")
    endif()
    list(PREPEND CMAKE_PREFIX_PATH "${ASCEND_CANN_PACKAGE_PATH}")
    find_package(ASC REQUIRED)
endif()
