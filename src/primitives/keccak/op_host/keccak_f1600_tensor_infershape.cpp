#include "register/op_impl_registry.h"

namespace ops {

static ge::graphStatus InferShape(gert::InferShapeContext* context)
{
    if (context == nullptr) {
        return ge::GRAPH_FAILED;
    }
    const gert::Shape* xShape = context->GetInputShape(0);
    gert::Shape* yShape = context->GetOutputShape(0);

    if (xShape == nullptr || yShape == nullptr || xShape->GetDimNum() != 2 || xShape->GetDim(0) != 50) {
        return ge::GRAPH_FAILED;
    }

    const int64_t batch = xShape->GetDim(1);
    yShape->SetDimNum(2);
    yShape->SetDim(0, 50);
    yShape->SetDim(1, batch);
    return ge::GRAPH_SUCCESS;
}

IMPL_OP_INFERSHAPE(KeccakF1600Tensor).InferShape(InferShape);

}  // namespace ops
