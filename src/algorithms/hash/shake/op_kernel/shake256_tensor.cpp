#include "shake256_tensor_tiling_data.h"
#include "shake_uniform_u32_tensor.h"

extern "C" __global__ __aicore__ void shake256_tensor(
    GM_ADDR x,
    GM_ADDR y,
    GM_ADDR workspace,
    GM_ADDR tiling)
{
    (void)workspace;
    REGISTER_TILING_DEFAULT(Shake256TensorTilingData);
    GET_TILING_DATA_WITH_STRUCT(Shake256TensorTilingData, tilingData, tiling);

    ShakeUniformU32Tensor::KernelShakeUniformU32Tensor<136U> op;
    op.Init(
        x,
        y,
        tilingData.batch,
        tilingData.inputLength,
        tilingData.outputLength,
        tilingData.groupSize);
    op.Process();
}
