#ifndef CANN_CRYPTO_SHAKE_TILING_H
#define CANN_CRYPTO_SHAKE_TILING_H

#include <cstdint>
#include <limits>

namespace cann_crypto::shake {
constexpr uint32_t MIN_GROUP_SIZE = 64U;
constexpr uint32_t MAX_GROUP_SIZE = 208U;
constexpr uint32_t MAX_BATCH_SIZE = std::numeric_limits<uint32_t>::max() - (MAX_GROUP_SIZE - 1U);
constexpr uint32_t MICRO_TILE = 16U;

struct ShakeSchedule {
    uint32_t groupSize;
    uint32_t blockDim;
};

inline uint32_t CeilDiv(uint32_t value, uint32_t divisor)
{
    return value / divisor + (value % divisor != 0U ? 1U : 0U);
}

inline bool ValidShape(uint64_t batch, uint64_t inputLength, uint64_t outputLength)
{
    const uint64_t maxWord = std::numeric_limits<uint32_t>::max();
    const uint64_t maxTensor = static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
    // 为内核分组向上取整的加法预留范围，张量总尺寸不得超过 INT64_MAX。
    return batch > 0U && batch <= MAX_BATCH_SIZE && inputLength <= maxWord &&
        outputLength > 0U && outputLength <= maxWord &&
        inputLength <= maxTensor / batch && outputLength <= maxTensor / batch;
}

inline ShakeSchedule MakeSchedule(uint32_t batch, uint32_t coreCount)
{
    coreCount = coreCount == 0U ? 1U : coreCount;
    const uint32_t perCore = CeilDiv(batch, coreCount);
    uint32_t group = perCore >= MAX_GROUP_SIZE ? MAX_GROUP_SIZE : CeilDiv(perCore, MICRO_TILE) * MICRO_TILE;
    group = group < MIN_GROUP_SIZE ? MIN_GROUP_SIZE : group;
    const uint32_t groups = CeilDiv(batch, group);
    return {group, groups < coreCount ? groups : coreCount};
}
} // namespace cann_crypto::shake
#endif
