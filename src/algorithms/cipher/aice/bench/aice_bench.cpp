/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#include "cann_crypto/aice.h"
#include "../op_host/aice_launch.h"
#include "aice_cpu_batch.h"
#include "acl/acl.h"

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
struct Options {
    uint32_t bytes{0}, batch{512}, dim{1}, warmup{3}, iterations{10};
};

std::vector<uint32_t> Sizes(const Options& options)
{
    if (options.bytes != 0)
        return {options.bytes};
    std::vector<uint32_t> sizes;
    for (uint32_t size = 1024; size <= 64U * 1024U * 1024U; size *= 4U)
        sizes.push_back(size);
    return sizes;
}

bool Parse(int argc, char** argv, Options& options)
{
    bool sizeSpecified = false;
    const char* names[] = {"--size-bytes", "--batch", "--dim", "--warmup", "--iterations"};
    uint32_t* values[] = {&options.bytes, &options.batch, &options.dim, &options.warmup, &options.iterations};
    for (int i = 1; i < argc; ++i) {
        size_t option = 0;
        for (; option < 5; ++option) {
            const size_t length = std::strlen(names[option]);
            if (std::strncmp(argv[i], names[option], length) != 0 || argv[i][length] != '=') {
                continue;
            }
            const char* text = argv[i] + length + 1;
            if (*text < '0' || *text > '9') {
                return false;
            }
            errno = 0;
            char* end = nullptr;
            const auto value = std::strtoull(text, &end, 10);
            if (errno == ERANGE || *end != '\0' || value > UINT32_MAX) {
                return false;
            }
            *values[option] = static_cast<uint32_t>(value);
            if (option == 0)
                sizeSpecified = true;
            break;
        }
        if (option == 5) {
            return false;
        }
    }
    return (!sizeSpecified || options.bytes > 0) && options.dim > 0 && options.iterations > 0 &&
           (options.batch == 128 || options.batch == 256 || options.batch == 512) &&
           static_cast<uint64_t>(options.batch) * options.dim <= UINT32_MAX;
}

struct Runtime {
    bool initialized{false}, deviceSet{false};
    aclrtStream stream{nullptr};
    aclrtEvent begin{nullptr}, end{nullptr};
    void* memory[4]{};
    ~Runtime()
    {
        // A failed launch/record can leave work queued; drain it before freeing buffers.
        if (stream != nullptr) {
            (void)aclrtSynchronizeStream(stream);
        }
        if (end != nullptr) {
            (void)aclrtDestroyEvent(end);
        }
        if (begin != nullptr) {
            (void)aclrtDestroyEvent(begin);
        }
        for (void* buffer : memory) {
            if (buffer != nullptr) {
                (void)aclrtFree(buffer);
            }
        }
        if (stream != nullptr) {
            (void)aclrtDestroyStream(stream);
        }
        if (deviceSet) {
            (void)aclrtResetDevice(CANN_DEVICE_ID);
        }
        if (initialized) {
            (void)aclFinalize();
        }
    }
};
} // namespace

#define CHECK_ACL(call)                                                               \
    do {                                                                              \
        const auto status = (call);                                                   \
        if (status != ACL_SUCCESS) {                                                  \
            std::fprintf(stderr, "%s failed: %d\n", #call, static_cast<int>(status)); \
            return 1;                                                                 \
        }                                                                             \
    } while (false)

int PrepareAndVerify(
    const Options& options, Runtime& runtime, const uint8_t (&key)[32], const uint8_t (&iv)[12],
    const std::vector<uint8_t>& plain, const std::vector<uint8_t>& expected)
{
    std::vector<uint8_t> actual(options.bytes);
    CHECK_ACL(aclInit(nullptr));
    runtime.initialized = true;
    CHECK_ACL(aclrtSetDevice(CANN_DEVICE_ID));
    runtime.deviceSet = true;
    CHECK_ACL(aclrtCreateStream(&runtime.stream));
    const size_t sizes[] = {options.bytes, options.bytes, sizeof(key), sizeof(iv)};
    for (size_t i = 0; i < 4; ++i) {
        CHECK_ACL(aclrtMalloc(&runtime.memory[i], sizes[i], ACL_MEM_MALLOC_HUGE_FIRST));
    }
    auto* input = static_cast<uint8_t*>(runtime.memory[0]);
    auto* output = static_cast<uint8_t*>(runtime.memory[1]);
    auto* deviceKey = static_cast<uint8_t*>(runtime.memory[2]);
    auto* deviceIv = static_cast<uint8_t*>(runtime.memory[3]);
    CHECK_ACL(aclrtMemcpy(input, options.bytes, plain.data(), options.bytes, ACL_MEMCPY_HOST_TO_DEVICE));
    CHECK_ACL(aclrtMemcpy(deviceKey, sizeof(key), key, sizeof(key), ACL_MEMCPY_HOST_TO_DEVICE));
    CHECK_ACL(aclrtMemcpy(deviceIv, sizeof(iv), iv, sizeof(iv), ACL_MEMCPY_HOST_TO_DEVICE));
    const auto encrypt = [&]() {
        return CannCryptoAiceEncrypt(
            input, options.bytes, output, options.bytes, deviceKey, deviceIv, options.batch, options.dim,
            runtime.stream);
    };
    CHECK_ACL(encrypt());
    CHECK_ACL(aclrtMemcpy(actual.data(), options.bytes, output, options.bytes, ACL_MEMCPY_DEVICE_TO_HOST));
    if (actual != expected) {
        std::fprintf(stderr, "ciphertext differs from reference\n");
        return 1;
    }
    return 0;
}

int Measure(const Options& options, Runtime& runtime)
{
    auto* input = static_cast<uint8_t*>(runtime.memory[0]);
    auto* output = static_cast<uint8_t*>(runtime.memory[1]);
    auto* deviceKey = static_cast<uint8_t*>(runtime.memory[2]);
    auto* deviceIv = static_cast<uint8_t*>(runtime.memory[3]);
    const auto launch = [&]() {
        return CannCryptoAiceLaunch(
            options.batch, true, options.dim, runtime.stream, input, deviceKey, deviceIv, output, options.bytes);
    };
    CHECK_ACL(aclrtCreateEventWithFlag(&runtime.begin, ACL_EVENT_TIME_LINE));
    CHECK_ACL(aclrtCreateEventWithFlag(&runtime.end, ACL_EVENT_TIME_LINE));
    for (uint32_t i = 0; i < options.warmup; ++i) {
        CHECK_ACL(launch());
    }
    CHECK_ACL(aclrtSynchronizeStream(runtime.stream));

    // Batch asynchronous launches between device events; synchronize only after the end event.
    CHECK_ACL(aclrtRecordEvent(runtime.begin, runtime.stream));
    for (uint32_t i = 0; i < options.iterations; ++i) {
        CHECK_ACL(launch());
    }
    CHECK_ACL(aclrtRecordEvent(runtime.end, runtime.stream));
    CHECK_ACL(aclrtSynchronizeStream(runtime.stream));
    float elapsedMs = 0.0F;
    CHECK_ACL(aclrtEventElapsedTime(&elapsedMs, runtime.begin, runtime.end));
    if (!std::isfinite(elapsedMs) || elapsedMs <= 0.0F) {
        std::fprintf(stderr, "invalid ACL event duration; increase --iterations\n");
        return 1;
    }
    const double averageUs = static_cast<double>(elapsedMs) * 1000.0 / options.iterations;
    std::printf(
        "AICE_BENCH operation=encrypt size_bytes=%u batch=%u dim=%u warmup=%u iterations=%u "
        "timing=acl-event kernel_us=%.3f Gbps=%.6f verified=1\n",
        options.bytes, options.batch, options.dim, options.warmup, options.iterations, averageUs,
        static_cast<double>(options.bytes) * 8.0 / averageUs / 1000.0);
    return 0;
}

int Run(const Options& options)
{
    uint8_t key[32]{}, iv[12]{};
    std::vector<uint8_t> plain(options.bytes), expected(options.bytes);
    for (size_t i = 0; i < sizeof(key); ++i) {
        key[i] = static_cast<uint8_t>(i);
    }
    for (size_t i = 0; i < sizeof(iv); ++i) {
        iv[i] = static_cast<uint8_t>(0xa0U + i);
    }
    for (size_t i = 0; i < plain.size(); ++i) {
        plain[i] = static_cast<uint8_t>(17U + 131U * i);
    }
    if (aice_cpu_batch_crypt(
            options.batch, options.dim, AICE_CPU_ENCRYPT, key, iv, plain.data(), expected.data(), options.bytes) !=
        AICE_CPU_SUCCESS) {
        std::fprintf(stderr, "reference encryption failed\n");
        return 1;
    }

    Runtime runtime;
    if (PrepareAndVerify(options, runtime, key, iv, plain, expected) != 0)
        return 1;
    return Measure(options, runtime);
}

int main(int argc, char** argv)
{
    Options options;
    const bool help = argc == 2 && std::strcmp(argv[1], "--help") == 0;
    if (help || !Parse(argc, argv, options)) {
        std::fprintf(
            help ? stdout : stderr, "usage: aice_bench [--size-bytes=N] [--batch=128|256|512] [--dim=N] "
                                    "[--warmup=N] [--iterations=N]\n"
                                    "default: dim=1, sizes=1KiB,4KiB,...,64MiB (x4), batch=512\n");
        return help ? 0 : 2;
    }
    for (uint32_t size : Sizes(options)) {
        options.bytes = size;
        const int status = Run(options);
        if (status != 0)
            return status;
    }
    return 0;
}
