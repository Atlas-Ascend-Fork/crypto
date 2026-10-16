/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * Licensed under CANN Open Software License Agreement Version 2.0.
 * See LICENSE in the root of the software repository for the full text.
 */
#include "../../csrc/acl_status.h"

#include <iostream>
#include <stdexcept>
#include <string>

int main()
{
    int failures = 0;
    const auto check = [&failures](bool condition) {
        if (!condition) {
            ++failures;
        }
    };

    bool outputReturned = false;
    const auto invokeHostWrapper = [&outputReturned](int syncStatus) {
        ascend_ops::detail::CheckAclSynchronize([syncStatus](int) { return syncStatus; }, 0, 0, "AES test kernel");
        outputReturned = true;
        return true;
    };
    check(invokeHostWrapper(0));
    check(outputReturned);

    outputReturned = false;
    try {
        invokeHostWrapper(17);
        check(false);
    } catch (const std::runtime_error& error) {
        check(std::string(error.what()) == "AES test kernel failed with ACL status 17");
    }
    check(!outputReturned);

    std::cout << "ACL_SYNC_STATUS_RESULT=" << (failures == 0 ? "PASS" : "FAIL") << '\n';
    return failures == 0 ? 0 : 1;
}
