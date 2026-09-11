#include "register/op_impl_registry.h"

namespace ge {

static graphStatus InferShape(gert::InferShapeContext* context)
{
    if (context == nullptr || context->GetOutputShape(0) == nullptr) {
        return GRAPH_FAILED;
    }

    // 调用方提供 y[B,O]，O 表示所需 XOF 输出长度；tiling 负责核对形状。
    return GRAPH_SUCCESS;
}

IMPL_OP_INFERSHAPE(Shake128Tensor).InferShape(InferShape);

}  // namespace ge
