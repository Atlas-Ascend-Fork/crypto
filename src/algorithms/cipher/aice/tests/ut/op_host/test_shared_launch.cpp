/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
// Test the production shared-library ABI without a device. Native launch
// arguments and multi-tile block processing are covered by ST.
#include "cann_crypto/aice.h"
#include "acl/acl.h"
#include <cstdio>
#include <initializer_list>

#define CHECK(x)                                                 \
    do {                                                         \
        if (!(x)) {                                              \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); \
            return 1;                                            \
        }                                                        \
    } while (false)
namespace {
int syncs = 0;
aclError syncError = ACL_SUCCESS;
} // namespace
extern "C" aclError aclrtSynchronizeStream(aclrtStream)
{
    ++syncs;
    return syncError;
}
int main()
{
    for (auto crypt : {CannCryptoAiceEncrypt, CannCryptoAiceDecrypt}) {
        for (uint32_t batch : {128U, 256U, 512U}) {
            syncs = 0;
            syncError = ACL_SUCCESS;
            CHECK(crypt(nullptr, 0, nullptr, 0, nullptr, nullptr, batch, 5, nullptr) == ACL_SUCCESS);
            CHECK(syncs == 1);
            syncError = 54321;
            CHECK(crypt(nullptr, 0, nullptr, 0, nullptr, nullptr, batch, 5, nullptr) == syncError);
            CHECK(syncs == 2);
            CHECK(crypt(nullptr, 1, nullptr, 1, nullptr, nullptr, batch, 5, nullptr) == ACL_ERROR_INVALID_PARAM);
            CHECK(syncs == 2);
        }
        CHECK(crypt(nullptr, 0, nullptr, 0, nullptr, nullptr, 64, 5, nullptr) == ACL_ERROR_INVALID_PARAM);
        CHECK(crypt(nullptr, 0, nullptr, 0, nullptr, nullptr, 128, 0, nullptr) == ACL_ERROR_INVALID_PARAM);
    }
    std::puts("AICE_SHARED_API_PASS");
}
