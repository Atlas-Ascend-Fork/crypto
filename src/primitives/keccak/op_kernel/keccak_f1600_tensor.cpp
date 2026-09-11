#include "keccak_f1600_tensor.h"

extern "C" __global__ __aicore__ void keccak_f1600_tensor(
    GM_ADDR x,
    GM_ADDR y,
    GM_ADDR workspace,
    GM_ADDR tiling)
{
    (void)workspace;
    REGISTER_TILING_DEFAULT(KeccakF1600TensorTilingData);
    GET_TILING_DATA_WITH_STRUCT(KeccakF1600TensorTilingData, tilingData, tiling);

    KeccakF1600TensorKernel::KernelKeccakF1600Tensor op;
    op.Init(x, y, &tilingData);
    op.Process();
}
