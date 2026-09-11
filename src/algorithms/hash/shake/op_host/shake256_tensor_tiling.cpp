#include "register/op_impl_registry.h"
#include "tiling/platform/platform_ascendc.h"
#include "../shake_internal.h"
#include "../op_kernel/shake256_tensor_tiling_data.h"
#include "../op_kernel/shake256_tensor_tiling_key.h"

namespace optiling {
static ge::graphStatus TilingFunc(gert::TilingContext *context)
{
    if (context == nullptr || context->GetInputShape(0) == nullptr || context->GetOutputShape(0) == nullptr) {
        return ge::GRAPH_FAILED;
    }
    const auto &input = context->GetInputShape(0)->GetStorageShape();
    const auto &output = context->GetOutputShape(0)->GetStorageShape();
    if (input.GetDimNum() != 2U || output.GetDimNum() != 2U ||
        input.GetDim(0) <= 0 || input.GetDim(1) < 0 || output.GetDim(1) <= 0 ||
        input.GetDim(0) != output.GetDim(0)) {
        return ge::GRAPH_FAILED;
    }
    uint32_t coreCount = 1U;
    if (context->GetPlatformInfo() != nullptr) {
        platform_ascendc::PlatformAscendC platform(context->GetPlatformInfo());
        coreCount = static_cast<uint32_t>(platform.GetCoreNumAiv());
    }
    // 将张量尺寸转换为内部请求，生成调度配置。
    cann_crypto::shake::LaunchConfig config{};
    const cann_crypto::shake::Request request{cann_crypto::shake::Algorithm::SHAKE256,
        static_cast<uint64_t>(input.GetDim(0)), static_cast<uint64_t>(input.GetDim(1)),
        static_cast<uint64_t>(output.GetDim(1))};
    if (!cann_crypto::shake::BuildLaunchConfig(request, coreCount, config)) {
        return ge::GRAPH_FAILED;
    }
    auto *tiling = context->GetTilingData<Shake256TensorTilingData>();
    if (tiling == nullptr || context->GetRawTilingData() == nullptr) {
        return ge::GRAPH_FAILED;
    }
    tiling->batch = config.batch;
    tiling->inputLength = config.inputLength;
    tiling->outputLength = config.outputLength;
    tiling->blockDim = config.blockDim;
    tiling->groupSize = config.groupSize;
    // 当前使用 uniform_u32 路径，保留字段置零。
    tiling->useFastPath = 0U;
    tiling->reserved0 = 0U;
    tiling->reserved1 = 0U;
    context->GetRawTilingData()->SetDataSize(sizeof(Shake256TensorTilingData));
    context->SetBlockDim(config.blockDim);
    context->SetTilingKey(SHAKE256_TENSOR_TILING_KEY_DEFAULT);
    auto *workspace = context->GetWorkspaceSizes(1);
    if (workspace == nullptr) {
        return ge::GRAPH_FAILED;
    }
    workspace[0] = 0U;
    return ge::GRAPH_SUCCESS;
}
IMPL_OP_OPTILING(Shake256Tensor).Tiling(TilingFunc);
} // namespace optiling
