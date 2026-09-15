# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

add_executable(keccak_infershape_test test_keccak_f1600_tensor_infershape.cpp)
target_compile_features(keccak_infershape_test PRIVATE cxx_std_17)
target_include_directories(keccak_infershape_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/stubs")
cann_warnings(keccak_infershape_test)
add_test(NAME keccak_infershape_test COMMAND keccak_infershape_test)

add_executable(keccak_tiling_test test_keccak_f1600_tensor_tiling.cpp)
target_compile_features(keccak_tiling_test PRIVATE cxx_std_17)
target_include_directories(keccak_tiling_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/stubs")
cann_warnings(keccak_tiling_test)
add_test(NAME keccak_tiling_test COMMAND keccak_tiling_test)
