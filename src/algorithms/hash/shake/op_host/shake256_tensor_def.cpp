#include "register/op_def_registry.h"

namespace ops {

class Shake256Tensor : public OpDef {
public:
    explicit Shake256Tensor(const char* name) : OpDef(name)
    {
        this->Input("x")
            .ParamType(REQUIRED)
            .DataType({ge::DT_UINT8})
            .Format({ge::FORMAT_ND})
            .UnknownShapeFormat({ge::FORMAT_ND});

        this->Output("y")
            .ParamType(REQUIRED)
            .DataType({ge::DT_UINT8})
            .Format({ge::FORMAT_ND})
            .UnknownShapeFormat({ge::FORMAT_ND});

        this->AICore()
            .AddConfig("ascend910b")
            .AddConfig("ascend910_93");
    }
};

OP_ADD(Shake256Tensor);

}  // namespace ops
