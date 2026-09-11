#include "../../../op_host/keccak_f1600_tensor_infershape.cpp"

#include <iostream>

int main()
{
    int failures = 0;
    auto check = [&failures](bool value) { if (!value) { ++failures; } };
    check(ops::InferShape(nullptr) == ge::GRAPH_FAILED);
    gert::Shape output;
    gert::InferShapeContext missingInput(nullptr, &output);
    check(ops::InferShape(&missingInput) == ge::GRAPH_FAILED);
    for (const gert::Shape& input : {gert::Shape{50}, gert::Shape{49, 64}, gert::Shape{50, 64, 1}}) {
        gert::InferShapeContext context(&input, &output);
        check(ops::InferShape(&context) == ge::GRAPH_FAILED);
    }
    for (const int64_t batch : {1, 64, 8192}) {
        gert::Shape input{50, batch};
        gert::InferShapeContext context(&input, &output);
        check(ops::InferShape(&context) == ge::GRAPH_SUCCESS);
        check(output.GetDimNum() == 2 && output.GetDim(0) == 50 && output.GetDim(1) == batch);
        gert::InferShapeContext missingOutput(&input, nullptr);
        check(ops::InferShape(&missingOutput) == ge::GRAPH_FAILED);
    }
    std::cout << "INFERSHAPE_RESULT=" << (failures == 0 ? "PASS" : "FAIL") << '\n';
    return failures == 0 ? 0 : 1;
}
