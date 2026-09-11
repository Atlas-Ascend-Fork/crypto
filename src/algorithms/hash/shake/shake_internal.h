#ifndef CANN_CRYPTO_SHAKE_INTERNAL_H
#define CANN_CRYPTO_SHAKE_INTERNAL_H

#include <cstdint>

namespace cann_crypto::shake {
// 内部请求和调度配置不依赖 CANN 类型。
enum class Algorithm : uint32_t {
    SHAKE128 = 128U,
    SHAKE256 = 256U
};

struct Request {
    Algorithm algorithm;
    uint64_t batch;
    uint64_t inputLength;
    uint64_t outputLength;
};

struct LaunchConfig {
    uint32_t batch;
    uint32_t inputLength;
    uint32_t outputLength;
    uint32_t rateBytes;
    uint32_t groupSize;
    uint32_t blockDim;
};

bool BuildLaunchConfig(const Request &request, uint32_t aivCoreCount, LaunchConfig &config);
} // namespace cann_crypto::shake
#endif
