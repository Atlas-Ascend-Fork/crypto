# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

add_executable(ntt_host_test test_ntt_host.cpp)
target_link_libraries(ntt_host_test PRIVATE cann_crypto_ntt)
target_compile_options(ntt_host_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME ntt_host COMMAND ntt_host_test)
add_test(NAME ntt_data COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_ntt_data.py")
add_test(NAME ntt_runner COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_ntt_runner.py")
set_tests_properties(ntt_host ntt_data ntt_runner PROPERTIES LABELS "ut;host_ut")
