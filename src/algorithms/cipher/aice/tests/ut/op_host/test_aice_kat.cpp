/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#include "aice_cpu_batch.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef AICE_KAT_DEVICE
#include "cann_crypto/aice.h"
#include "acl/acl.h"
#endif

using Bytes = std::vector<uint8_t>;
static Bytes Hex(const std::string& text)
{
    if (text.size() % 2)
        throw std::runtime_error("odd KAT hex length");
    Bytes result;
    for (size_t i = 0; i < text.size(); i += 2) {
        auto digit = [](char c) -> unsigned {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            if (c >= 'A' && c <= 'F')
                return c - 'A' + 10;
            throw std::runtime_error("invalid KAT hex");
        };
        result.push_back(static_cast<uint8_t>(digit(text[i]) * 16 + digit(text[i + 1])));
    }
    return result;
}

static void Crypt(bool decrypt, uint32_t batch, const Bytes& key, const Bytes& iv, const Bytes& input, Bytes& output)
{
#ifdef AICE_KAT_DEVICE
    void* buffers[4]{};
    const size_t sizes[] = {input.size(), output.size(), key.size(), iv.size()};
    const void* host[] = {input.data(), nullptr, key.data(), iv.data()};
    auto check = [](aclError status) {
        if (status != ACL_SUCCESS)
            throw std::runtime_error("ACL KAT error " + std::to_string(status));
    };
    try {
        for (size_t i = 0; i < 4 && !input.empty(); ++i) {
            check(aclrtMalloc(&buffers[i], sizes[i], ACL_MEM_MALLOC_HUGE_FIRST));
            if (host[i])
                check(aclrtMemcpy(buffers[i], sizes[i], host[i], sizes[i], ACL_MEMCPY_HOST_TO_DEVICE));
        }
        auto crypt = decrypt ? CannCryptoAiceDecrypt : CannCryptoAiceEncrypt;
        check(crypt(
            static_cast<uint8_t*>(buffers[0]), input.size(), static_cast<uint8_t*>(buffers[1]), output.size(),
            static_cast<uint8_t*>(buffers[2]), static_cast<uint8_t*>(buffers[3]), batch, 1, nullptr));
        if (!output.empty())
            check(aclrtMemcpy(output.data(), output.size(), buffers[1], output.size(), ACL_MEMCPY_DEVICE_TO_HOST));
    } catch (...) {
        for (auto buffer : buffers)
            if (buffer)
                (void)aclrtFree(buffer);
        throw;
    }
    for (auto buffer : buffers)
        if (buffer)
            check(aclrtFree(buffer));
#else
    if (aice_cpu_batch_crypt(
            batch, 1, decrypt ? AICE_CPU_DECRYPT : AICE_CPU_ENCRYPT, key.data(), iv.data(), input.data(), output.data(),
            static_cast<uint32_t>(input.size())) != AICE_CPU_SUCCESS)
        throw std::runtime_error("CPU KAT error");
#endif
}

static void CheckVector(const Bytes& key, const Bytes& iv, uint32_t lane, const Bytes& plain, const Bytes& cipher)
{
    if (key.size() != 32 || iv.size() != 12 || plain.size() != cipher.size())
        throw std::runtime_error("malformed KAT vector");
    // A per-lane word w is stored at uint16 index w*batch+lane. This works
    // across 32-word periods as well; no expected byte is recomputed.
    for (uint32_t batch : {128U, 256U, 512U}) {
        if (lane >= batch)
            throw std::runtime_error("unsupported KAT lane");
        const size_t bytes = (plain.size() + 1) / 2 * batch * 2;
        for (bool decrypt : {false, true}) {
            const auto& src = decrypt ? cipher : plain;
            const auto& expected = decrypt ? plain : cipher;
            Bytes input(bytes, 0), output(bytes);
            for (size_t i = 0; i < src.size(); ++i)
                input[(i / 2 * batch + lane) * 2 + i % 2] = src[i];
            Crypt(decrypt, batch, key, iv, input, output);
            for (size_t i = 0; i < expected.size(); ++i)
                if (output[(i / 2 * batch + lane) * 2 + i % 2] != expected[i])
                    throw std::runtime_error(
                        "KAT mismatch batch=" + std::to_string(batch) + " lane=" + std::to_string(lane) +
                        " byte=" + std::to_string(i));
        }
    }
}

struct KatVector {
    Bytes key, iv, plain;
    uint32_t lane = 0;
    size_t declaredLength = 0;
    bool keystream = false;
};

static bool ReadField(const std::string& field, const std::string& value, KatVector& vector)
{
    if (field == "COUNT") {
        vector.key.clear();
        vector.iv.clear();
        vector.plain.clear();
    } else if (field == "KEY")
        vector.key = Hex(value);
    else if (field == "IV")
        vector.iv = Hex(value);
    else if (field == "LANE")
        vector.lane = static_cast<uint32_t>(std::stoul(value));
    else if (field == "NWORDS")
        vector.declaredLength = 2 * std::stoul(value);
    else if (field == "LEN")
        vector.declaredLength = std::stoul(value);
    else if (field == "PT")
        vector.plain = Hex(value);
    else if ((field == "KS" && vector.keystream) || (field == "CT" && !vector.keystream)) {
        const auto cipher = Hex(value);
        if (cipher.size() != vector.declaredLength)
            throw std::runtime_error("KAT length mismatch");
        if (vector.keystream)
            vector.plain.assign(cipher.size(), 0);
        CheckVector(vector.key, vector.iv, vector.lane, vector.plain, cipher);
        return true;
    } else
        throw std::runtime_error("unexpected KAT field");
    return false;
}

static bool ReadLine(std::string line, KatVector& vector)
{
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
    if (line.empty() || line[0] == '#')
        return false;
    if (line == "[KEYSTREAM]" || line == "[ENCRYPT]") {
        vector.keystream = line == "[KEYSTREAM]";
        return false;
    }
    const auto split = line.find(" =");
    if (split == std::string::npos)
        throw std::runtime_error("invalid KAT field");
    auto value = line.substr(split + 2);
    if (!value.empty() && value[0] == ' ')
        value.erase(0, 1);
    return ReadField(line.substr(0, split), value, vector);
}

static void RunKats(bool smoke)
{
    std::ifstream file(AICE_KAT_PATH);
    if (!file)
        throw std::runtime_error("cannot open KAT file");
    KatVector vector;
    size_t passed = 0;
    std::string line;
    while (std::getline(file, line)) {
        if (ReadLine(line, vector)) {
            ++passed;
            if (smoke)
                break;
        }
    }
    if (passed != (smoke ? 1U : 29U))
        throw std::runtime_error("incomplete KAT coverage");
    std::printf(
        "AICE_KAT_PASS vectors=%zu batches=128,256,512 encrypt+decrypt backend=%s\n", passed,
#ifdef AICE_KAT_DEVICE
        "device"
#else
        "submission-cpu"
#endif
    );
}

int main(int argc, char** argv)
{
    const bool smoke = argc == 2 && std::string(argv[1]) == "--smoke";
    if (argc != 1 && !smoke)
        return 2;
#ifdef AICE_KAT_DEVICE
    if (aclInit(nullptr) != ACL_SUCCESS || aclrtSetDevice(CANN_DEVICE_ID) != ACL_SUCCESS)
        return 1;
#endif
    int status = 0;
    try {
        RunKats(smoke);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        status = 1;
    }
#ifdef AICE_KAT_DEVICE
    if (aclrtResetDevice(CANN_DEVICE_ID) != ACL_SUCCESS || aclFinalize() != ACL_SUCCESS)
        status = 1;
#endif
    return status;
}
