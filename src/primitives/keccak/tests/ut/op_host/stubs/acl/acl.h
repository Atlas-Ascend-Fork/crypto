/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_CRYPTO_TEST_ACL_H
#define CANN_CRYPTO_TEST_ACL_H

#include <stdint.h>

typedef int32_t aclError;
typedef void* aclrtStream;
enum aclrtDevAttr { ACL_DEV_ATTR_VECTOR_CORE_NUM };
enum { ACL_SUCCESS = 0, ACL_ERROR_INVALID_PARAM = 100000 };

#ifdef __cplusplus
extern "C" {
#endif
aclError aclrtGetDevice(int32_t* device);
aclError aclrtGetDeviceInfo(uint32_t device, aclrtDevAttr attr, int64_t* value);
aclError aclrtSynchronizeStream(aclrtStream stream);
#ifdef __cplusplus
}
#endif
#endif
