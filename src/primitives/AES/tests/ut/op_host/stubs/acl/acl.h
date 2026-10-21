/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#pragma once
#include <cstddef>
#include <cstdint>
using aclError = int32_t;
using aclrtStream = void*;
constexpr aclError ACL_SUCCESS = 0;
constexpr aclError ACL_ERROR_INVALID_PARAM = 100000;
constexpr int ACL_MEM_MALLOC_HUGE_FIRST = 0;
constexpr int ACL_MEMCPY_HOST_TO_DEVICE = 1;
constexpr int ACL_MEMCPY_DEVICE_TO_DEVICE = 3;
aclError aclrtMalloc(void** pointer, size_t bytes, int policy);
aclError aclrtFree(void* pointer);
aclError aclrtMemcpy(void* destination, size_t capacity, const void* source, size_t bytes, int kind);
aclError aclrtMemset(void* destination, size_t capacity, int value, size_t bytes);
aclError aclrtSynchronizeStream(aclrtStream stream);
