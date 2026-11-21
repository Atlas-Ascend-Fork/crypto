# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# Licensed under CANN Open Software License Agreement Version 2.0. See LICENSE.
cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(cann_crypto_sm3_reference LANGUAGES C)
endif()
add_library(cann_crypto_sm3_reference STATIC sm3_ref.c)
target_include_directories(cann_crypto_sm3_reference PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(cann_crypto_sm3_reference PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
