/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "cann_crypto/keccak.h"
#include "acl_example.h"

#include <iostream>

int main(int argc, char** argv)
{
    try {
        const int device = argc > 1 ? std::stoi(argv[1]) : 0;
        example::Session session(device);
        constexpr int64_t batch = 64;
        std::vector<uint32_t> input(50 * batch, 0U);
        std::vector<uint32_t> output(input.size());
        example::Buffer x(input.size() * sizeof(uint32_t));
        example::Buffer y(output.size() * sizeof(uint32_t));
        example::Check(
            aclrtMemcpy(x.data(), x.size(), input.data(), x.size(), ACL_MEMCPY_HOST_TO_DEVICE), "copy input");
        example::Check(
            CannCryptoKeccakF1600(
                static_cast<const uint32_t*>(x.data()), static_cast<uint32_t*>(y.data()), batch, session.stream()),
            "Keccak kernel launch");
        example::Check(
            aclrtMemcpy(output.data(), y.size(), y.data(), y.size(), ACL_MEMCPY_DEVICE_TO_HOST), "copy output");

        // 全零初态经 24 轮置换后，第一个 lane 为 0xf1258f7940e1dde7。
        for (size_t message = 0; message < static_cast<size_t>(batch); ++message) {
            if (output[message] != 0x40e1dde7U || output[batch + message] != 0xf1258f79U) {
                throw std::runtime_error("Keccak output mismatch");
            }
        }
        std::cout << "KeccakF1600Tensor example: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
