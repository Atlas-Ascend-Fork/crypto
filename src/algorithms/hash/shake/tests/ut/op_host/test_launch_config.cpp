#include "algorithms/hash/shake/shake_internal.h"
#include "algorithms/hash/shake/op_host/shake_tiling.h"

#include <cstdint>
#include <iostream>
#include <limits>

namespace {
int failures = 0;
void Check(bool condition, int line)
{
    if (!condition) {
        std::cerr << "check failed at line " << line << '\n';
        ++failures;
    }
}
}
#define CHECK(value) Check((value), __LINE__)

int main()
{
    using namespace cann_crypto::shake;
    LaunchConfig config{};
    for (const auto algorithm : {Algorithm::SHAKE128, Algorithm::SHAKE256}) {
        for (const uint32_t cores : {0U, 1U, 20U, 40U}) {
            for (const uint32_t batch : {1U, 63U, 64U, 65U, 207U, 208U, 209U, 8192U, MAX_BATCH_SIZE}) {
                const Request request{algorithm, batch, 0U, 1U};
                CHECK(BuildLaunchConfig(request, cores, config));
                CHECK(config.batch == batch && config.inputLength == 0U && config.outputLength == 1U);
                CHECK(config.rateBytes == (algorithm == Algorithm::SHAKE128 ? 168U : 136U));
                CHECK(config.groupSize >= 64U && config.groupSize <= 208U && config.groupSize % 16U == 0U);
                CHECK(config.blockDim >= 1U && config.blockDim <= (cores == 0U ? 1U : cores));
                CHECK(CeilDiv(batch, config.groupSize) >= config.blockDim);
            }
        }
    }
    CHECK(!BuildLaunchConfig({Algorithm::SHAKE128, 0, 64, 32}, 20, config));
    CHECK(!BuildLaunchConfig({Algorithm::SHAKE128, MAX_BATCH_SIZE + 1U, 0, 1}, 20, config));
    CHECK(!BuildLaunchConfig({Algorithm::SHAKE256, UINT32_MAX, 0, 1}, 20, config));
    CHECK(!BuildLaunchConfig({Algorithm::SHAKE128, 1, 64, 0}, 20, config));
    CHECK(!BuildLaunchConfig({static_cast<Algorithm>(0), 1, 64, 32}, 20, config));
    CHECK(!BuildLaunchConfig({Algorithm::SHAKE128, UINT64_C(1) << 32, 1, 1}, 20, config));
    CHECK(!BuildLaunchConfig({Algorithm::SHAKE128, 1, UINT64_C(1) << 32, 1}, 20, config));
    CHECK(!BuildLaunchConfig({Algorithm::SHAKE128, 1, 0, UINT64_C(1) << 32}, 20, config));
    CHECK(!BuildLaunchConfig({Algorithm::SHAKE128, UINT32_MAX, UINT32_MAX, UINT32_MAX}, 20, config));
    CHECK(BuildLaunchConfig({Algorithm::SHAKE256, 1, UINT32_MAX, UINT32_MAX}, 20, config));
    std::cout << "SHA3_LAUNCH_CONFIG=" << (failures == 0 ? "PASS" : "FAIL") << '\n';
    return failures == 0 ? 0 : 1;
}
