/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef CANN_CRYPTO_DEVICE_QUALIFIERS_H
#define CANN_CRYPTO_DEVICE_QUALIFIERS_H
// The host model executes the exact scalar device code, but proves no NPU behavior.
#if defined(CANN_CRYPTO_DEVICE_MODEL)
#include <cstdint>
#include <cstddef>
#define CANN_CRYPTO_DEVICE_INLINE inline
#else
#include "kernel_operator.h"
#define CANN_CRYPTO_DEVICE_INLINE __aicore__ inline
#endif
#endif
