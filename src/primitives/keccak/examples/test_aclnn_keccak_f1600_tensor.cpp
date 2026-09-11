#include <aclnn_keccak_f1600_tensor.h>
#include "acl_example.h"

#include <iostream>

int main(int argc, char** argv)
{
    try {
        const int device = argc > 1 ? std::stoi(argv[1]) : 0;
        example::Session session(device);
        constexpr int64_t batch = 64;
        std::vector<uint32_t> input(50 * batch, 0U);
        std::vector<uint32_t> output(input.size());
        example::Buffer x(input.size() * sizeof(uint32_t));
        example::Buffer y(output.size() * sizeof(uint32_t));
        example::Check(aclrtMemcpy(x.data(), x.size(), input.data(), x.size(), ACL_MEMCPY_HOST_TO_DEVICE), "copy input");
        example::Tensor inputTensor(x, {50, batch}, ACL_UINT32);
        example::Tensor outputTensor(y, {50, batch}, ACL_UINT32);

        uint64_t workspaceSize = 0;
        aclOpExecutor* executor = nullptr;
        example::Check(aclnnKeccakF1600TensorGetWorkspaceSize(inputTensor.get(), outputTensor.get(),
            &workspaceSize, &executor), "workspace query");
        example::Buffer workspace(workspaceSize);
        example::Check(aclnnKeccakF1600Tensor(workspaceSize == 0 ? nullptr : workspace.data(),
            workspaceSize, executor, session.stream()), "KeccakF1600Tensor");
        example::Check(aclrtSynchronizeStream(session.stream()), "stream synchronize");
        example::Check(aclrtMemcpy(output.data(), y.size(), y.data(), y.size(), ACL_MEMCPY_DEVICE_TO_HOST), "copy output");

        // 全零初态经 24 轮置换后，第一个 lane 为 0xf1258f7940e1dde7。
        for (size_t message = 0; message < static_cast<size_t>(batch); ++message) {
            if (output[message] != 0x40e1dde7U || output[batch + message] != 0xf1258f79U) {
                throw std::runtime_error("Keccak output mismatch");
            }
        }
        std::cout << "KeccakF1600Tensor example: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
