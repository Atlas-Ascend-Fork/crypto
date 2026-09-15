/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <aclnn_shake128_tensor.h>
#include <aclnn_shake256_tensor.h>
#include "acl_example.h"

#include <array>
#include <iostream>

int main(int argc, char** argv)
{
    try {
        const int device = argc > 1 ? std::stoi(argv[1]) : 0;
        const int bits = argc > 2 ? std::stoi(argv[2]) : 128;
        if (bits != 128 && bits != 256) { throw std::runtime_error("algorithm must be 128 or 256"); }
        example::Session session(device);
        constexpr int64_t batch = 2;
        constexpr int64_t outputLength = 32;
        example::Buffer x(0);
        example::Buffer y(batch * outputLength);
        example::Tensor inputTensor(x, {batch, 0}, ACL_UINT8);
        example::Tensor outputTensor(y, {batch, outputLength}, ACL_UINT8);
        uint64_t workspaceSize = 0;
        aclOpExecutor* executor = nullptr;
        const auto getWorkspace = bits == 128 ? aclnnShake128TensorGetWorkspaceSize : aclnnShake256TensorGetWorkspaceSize;
        const auto launch = bits == 128 ? aclnnShake128Tensor : aclnnShake256Tensor;
        example::Check(getWorkspace(inputTensor.get(), outputTensor.get(), &workspaceSize, &executor), "workspace query");
        example::Buffer workspace(workspaceSize);
        example::Check(launch(workspaceSize == 0 ? nullptr : workspace.data(), workspaceSize,
            executor, session.stream()), "SHAKE");
        example::Check(aclrtSynchronizeStream(session.stream()), "stream synchronize");
        std::vector<uint8_t> output(y.size());
        example::Check(aclrtMemcpy(output.data(), output.size(), y.data(), y.size(), ACL_MEMCPY_DEVICE_TO_HOST), "copy output");

        const std::array<uint8_t, 16> expected128 = {
            0x7f, 0x9c, 0x2b, 0xa4, 0xe8, 0x8f, 0x82, 0x7d, 0x61, 0x60, 0x45, 0x50, 0x76, 0x05, 0x85, 0x3e};
        const std::array<uint8_t, 16> expected256 = {
            0x46, 0xb9, 0xdd, 0x2b, 0x0b, 0xa8, 0x8d, 0x13, 0x23, 0x3b, 0x3f, 0xeb, 0x74, 0x3e, 0xeb, 0x24};
        const auto& expected = bits == 128 ? expected128 : expected256;
        for (size_t message = 0; message < static_cast<size_t>(batch); ++message) {
            for (size_t byte = 0; byte < expected.size(); ++byte) {
                if (output[message * outputLength + byte] != expected[byte]) {
                    throw std::runtime_error("SHAKE output mismatch");
                }
            }
        }
        std::cout << "SHAKE" << bits << " example: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
