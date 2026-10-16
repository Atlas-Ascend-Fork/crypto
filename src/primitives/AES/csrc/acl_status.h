/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * Licensed under CANN Open Software License Agreement Version 2.0.
 * See LICENSE in the root of the software repository for the full text.
 */
#pragma once

#include <stdexcept>
#include <string>

namespace ascend_ops::detail {

template <typename Synchronize, typename Stream, typename Status>
void CheckAclSynchronize(Synchronize synchronize, Stream stream, Status success, const char* operation)
{
    const auto status = synchronize(stream);
    if (status != success) {
        throw std::runtime_error(
            std::string(operation) + " failed with ACL status " + std::to_string(static_cast<long long>(status)));
    }
}

} // namespace ascend_ops::detail
