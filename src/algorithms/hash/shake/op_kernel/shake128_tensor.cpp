#include "shake128_tensor_tiling_data.h"
#include "shake_uniform_u32_tensor.h"

extern "C" __global__ __aicore__ void shake128_tensor(
    GM_ADDR x,
    GM_ADDR y,
    GM_ADDR workspace,
    GM_ADDR tiling)
{
    (void)workspace;
    REGISTER_TILING_DEFAULT(Shake128TensorTilingData);
    GET_TILING_DATA_WITH_STRUCT(Shake128TensorTilingData, tilingData, tiling);

    ShakeUniformU32Tensor::KernelShakeUniformU32Tensor<168U> op;
    op.Init(
        x,
        y,
        tilingData.batch,
        tilingData.inputLength,
        tilingData.outputLength,
        tilingData.groupSize);
    op.Process();
}
