/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
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
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace cann_crypto::ntt::test {

template <typename T>
bool ReadBinary(const std::string& path, std::vector<T>& out)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return false;
    }
    const std::streamsize bytes = file.tellg();
    if (bytes < 0 || bytes % static_cast<std::streamsize>(sizeof(T)) != 0) {
        return false;
    }
    out.resize(static_cast<size_t>(bytes) / sizeof(T));
    file.seekg(0, std::ios::beg);
    return file.read(reinterpret_cast<char*>(out.data()), bytes).good();
}

inline std::vector<int8_t> PrepackBatMatricesToB1Nz(const std::vector<int8_t>& raw, size_t n)
{
    constexpr size_t block = 32;
    if (n == 0) {
        return {};
    }
    if (n % block != 0 || n > std::numeric_limits<size_t>::max() / n) {
        return {};
    }
    const size_t matrixBytes = n * n;
    if (raw.size() % matrixBytes != 0) {
        return {};
    }
    std::vector<int8_t> packed(raw.size(), 0);
    const size_t blocks = n / block;
    for (size_t matrix = 0; matrix < raw.size(); matrix += matrixBytes) {
        for (size_t row = 0; row < n; ++row) {
            for (size_t col = 0; col < n; ++col) {
                const size_t tile = (col / block) * blocks + row / block;
                const size_t offset = tile * block * block + (row % block) * block + col % block;
                packed[matrix + offset] = raw[matrix + row * n + col];
            }
        }
    }
    return packed;
}
} // namespace cann_crypto::ntt::test
