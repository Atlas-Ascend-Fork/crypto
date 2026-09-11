#include "tiling_test_compat.h"
#include "../../../op_kernel/keccak_f1600_tensor_tiling_data.h"

#include <array>
#include <cstdint>
#include <cstring>

int main()
{
    const KeccakF1600TensorTilingData expected{257U};
    // 非对齐字节地址也应通过 memcpy 正确读取，不依赖指针强转。
    std::array<uint8_t, sizeof(expected) + 1> bytes{};
    std::memcpy(bytes.data() + 1, &expected, sizeof(expected));
    GET_TILING_DATA_WITH_STRUCT(KeccakF1600TensorTilingData, actual, bytes.data() + 1);
    if (actual.batch != expected.batch) {
        return 1;
    }
    bytes.fill(0);
    return actual.batch == expected.batch ? 0 : 1;
}
