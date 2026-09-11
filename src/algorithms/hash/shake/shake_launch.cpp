#include "shake_internal.h"
#include "op_host/shake_tiling.h"

namespace cann_crypto::shake {
bool BuildLaunchConfig(const Request &request, uint32_t aivCoreCount, LaunchConfig &config)
{
    if ((request.algorithm != Algorithm::SHAKE128 && request.algorithm != Algorithm::SHAKE256) ||
        !ValidShape(request.batch, request.inputLength, request.outputLength)) {
        return false;
    }
    const auto batch = static_cast<uint32_t>(request.batch);
    const auto schedule = MakeSchedule(batch, aivCoreCount);
    // 当前只有统一 U32 张量路径，算法选择只影响 sponge 的 rate。
    config = {batch, static_cast<uint32_t>(request.inputLength), static_cast<uint32_t>(request.outputLength),
        request.algorithm == Algorithm::SHAKE128 ? 168U : 136U, schedule.groupSize, schedule.blockDim};
    return true;
}
} // namespace cann_crypto::shake
