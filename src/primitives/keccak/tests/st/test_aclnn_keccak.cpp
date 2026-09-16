/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "keccak_test_utils.h"
#include "../../examples/acl_example.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

using cann_crypto::test::RequireAcl;
using cann_crypto::test::RunKeccakCase;

int main(int argc, char** argv)
{
    aclrtStream stream = nullptr;
    bool passed = true;
    const int deviceId = argc > 1 ? std::stoi(argv[1]) : 0;
    try {
        RequireAcl(aclInit(nullptr), "aclInit");
        RequireAcl(aclrtSetDevice(deviceId), "aclrtSetDevice");
        example::CheckDeviceTarget();
        RequireAcl(aclrtCreateStream(&stream), "aclrtCreateStream");
        for (const size_t batch : {64U, 256U, 1024U, 4096U, 8192U}) {
            passed = RunKeccakCase(batch, stream) && passed;
        }
        RequireAcl(aclrtDestroyStream(stream), "aclrtDestroyStream");
        stream = nullptr;
        RequireAcl(aclrtResetDevice(deviceId), "aclrtResetDevice");
        RequireAcl(aclFinalize(), "aclFinalize");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        if (stream != nullptr) {
            aclrtDestroyStream(stream);
        }
        aclrtResetDevice(deviceId);
        aclFinalize();
        passed = false;
    }
    std::cout << "CORRECTNESS_RESULT=" << (passed ? "PASS" : "FAIL") << '\n';
    return passed ? 0 : 1;
}
