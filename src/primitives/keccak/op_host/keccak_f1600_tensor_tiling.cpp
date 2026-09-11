#include "register/op_impl_registry.h"
#include "tiling/platform/platform_ascendc.h"
#include "../op_kernel/keccak_f1600_tensor_tiling_data.h"
#include "../op_kernel/keccak_f1600_tensor_tiling_key.h"

namespace optiling {

static ge::graphStatus TilingFunc(gert::TilingContext* context)
{
    if (context == nullptr || context->GetPlatformInfo() == nullptr) {
        return ge::GRAPH_FAILED;
    }
    const gert::StorageShape* xShape = context->GetInputShape(0);
    const gert::StorageShape* yShape = context->GetOutputShape(0);
    if (xShape == nullptr || yShape == nullptr) {
        return ge::GRAPH_FAILED;
    }

    const gert::Shape& shape = xShape->GetStorageShape();
    const auto& output = yShape->GetStorageShape();
    if (shape.GetDimNum() != 2 || shape.GetDim(0) != 50 ||
        output.GetDimNum() != 2 || output.GetDim(0) != 50 ||
        output.GetDim(1) != shape.GetDim(1)) {
        return ge::GRAPH_FAILED;
    }

    const int64_t batch64 = shape.GetDim(1);
    if (batch64 <= 0 || batch64 > 8192) {
        return ge::GRAPH_FAILED;
    }

    constexpr uint32_t tile = 256;

    const uint32_t batch = static_cast<uint32_t>(batch64);
    const uint32_t tileCount = (batch + tile - 1U) / tile;
    auto ascendcPlatform = platform_ascendc::PlatformAscendC(context->GetPlatformInfo());
    uint32_t coreNum = static_cast<uint32_t>(ascendcPlatform.GetCoreNumAiv());
    if (coreNum == 0U) {
        coreNum = 20U;
    }
    const uint32_t blockDim = tileCount < coreNum ? tileCount : coreNum;

    KeccakF1600TensorTilingData* tiling =
        context->GetTilingData<KeccakF1600TensorTilingData>();
    if (tiling == nullptr || context->GetRawTilingData() == nullptr) {
        return ge::GRAPH_FAILED;
    }
    tiling->batch = batch;

    context->SetBlockDim(blockDim);
    context->SetTilingKey(KECCAK_F1600_TENSOR_TILING_KEY_DEFAULT);
    auto* workspace = context->GetWorkspaceSizes(1);
    if (workspace == nullptr) {
        return ge::GRAPH_FAILED;
    }
    workspace[0] = 0U;
    context->GetRawTilingData()->SetDataSize(sizeof(KeccakF1600TensorTilingData));
    return ge::GRAPH_SUCCESS;
}

IMPL_OP_OPTILING(KeccakF1600Tensor).Tiling(TilingFunc);

}  // namespace optiling
