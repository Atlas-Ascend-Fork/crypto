#ifndef CANN_CRYPTO_TILING_TEST_COMPAT_H
#define CANN_CRYPTO_TILING_TEST_COMPAT_H

#ifndef CANN_CRYPTO_KERNEL_UT
#error "This header is only for Kernel unit tests"
#endif

#include <cstring>
#include <type_traits>

namespace cann_crypto::test {
template<typename T>
inline T ReadTiling(const void* source)
{
    static_assert(std::is_trivially_copyable<T>::value, "Tiling data must be trivially copyable");
    T data{};
    std::memcpy(&data, source, sizeof(T));
    return data;
}
} // namespace cann_crypto::test

// CPU UT 的 Tiling 缓冲区由 GmAlloc 分配并写入标准结构体。
// 优先使用工具链实现，仅在未提供宏时补充测试侧读取方式。
#ifndef GET_TILING_DATA_WITH_STRUCT
#define GET_TILING_DATA_WITH_STRUCT(type, name, address) \
    type name = ::cann_crypto::test::ReadTiling<type>(address)
#endif

#endif
