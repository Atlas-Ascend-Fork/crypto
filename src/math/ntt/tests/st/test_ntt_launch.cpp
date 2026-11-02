/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "acl/acl.h"
#include "op_kernel/ntt_tiling_data.h"
#include <cstdio>

extern "C" aclError CannCryptoNttLaunch(
    uint32_t blockDim, void* stream, void* x, void* matrixPhase1, void* matrixPhase2, void* twiddlePhase12, void* y,
    void* workspace, NttTilingData tiling);

int main()
{
    if (aclInit(nullptr) != ACL_SUCCESS) {
        return 1;
    }
    if (aclrtSetDevice(0) != ACL_SUCCESS) {
        (void)aclFinalize();
        return 1;
    }
    aclrtStream stream = nullptr;
    if (aclrtCreateStream(&stream) != ACL_SUCCESS) {
        (void)aclrtResetDevice(0);
        (void)aclFinalize();
        return 1;
    }
    NttTilingData tiling{};
    // Zero blocks must fail at submission, before the kernel executes.
    const auto invalid = CannCryptoNttLaunch(0, stream, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, tiling);
    // A null workspace makes the kernel return immediately. A valid submission
    // must not inherit the previous launch's error; completion is checked separately.
    const auto valid = CannCryptoNttLaunch(1, stream, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, tiling);
    const auto completion = aclrtSynchronizeStream(stream);
    const bool passed = invalid != ACL_SUCCESS && valid == ACL_SUCCESS && completion == ACL_SUCCESS;
    std::printf("NTT launch status: invalid=%d valid=%d completion=%d\n", invalid, valid, completion);
    (void)aclrtDestroyStream(stream);
    (void)aclrtResetDevice(0);
    (void)aclFinalize();
    return passed ? 0 : 1;
}
