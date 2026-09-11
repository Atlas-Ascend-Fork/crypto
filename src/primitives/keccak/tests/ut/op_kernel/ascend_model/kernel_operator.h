#ifndef CANN_CRYPTO_TEST_ASCEND_MODEL_H
#define CANN_CRYPTO_TEST_ASCEND_MODEL_H

#include <algorithm>
#include <cstdint>
#include <stdexcept>

// Host 算术模型只模拟立即完成的向量运算，不模拟流水线、屏障或 DMA。
#define __aicore__
constexpr int PIPE_V = 0;
namespace AscendC {
template <typename T>
class LocalTensor {
public:
    explicit LocalTensor(T *pointer) : pointer_(pointer) {}
    LocalTensor operator[](uint32_t offset) const { return LocalTensor(pointer_ + offset); }
    template <typename U>
    LocalTensor<U> ReinterpretCast() const { return LocalTensor<U>(reinterpret_cast<U *>(pointer_)); }
    T *Data() const { return pointer_; }
private:
    T *pointer_;
};
template <int Pipe>
void PipeBarrier() {}
template <typename T>
void DataCopy(LocalTensor<T> dst, LocalTensor<T> src, uint32_t count)
{
    std::copy_n(src.Data(), count, dst.Data());
}
template <typename T>
void Duplicate(LocalTensor<T> dst, T value, uint32_t count)
{
    std::fill_n(dst.Data(), count, value);
}
template <typename T>
void Xor(LocalTensor<T> dst, LocalTensor<T> left, LocalTensor<T> right, uint32_t count)
{
    // 与 CPU 调试路径一致，禁止异或结果覆盖仍需读取的输入。
    if (dst.Data() == left.Data() || dst.Data() == right.Data()) {
        throw std::logic_error("Xor requires a destination distinct from its inputs");
    }
    for (uint32_t i = 0; i < count; ++i) { dst.Data()[i] = left.Data()[i] ^ right.Data()[i]; }
}
template <typename T>
void And(LocalTensor<T> dst, LocalTensor<T> left, LocalTensor<T> right, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) { dst.Data()[i] = left.Data()[i] & right.Data()[i]; }
}
template <typename T>
void Or(LocalTensor<T> dst, LocalTensor<T> left, LocalTensor<T> right, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) { dst.Data()[i] = left.Data()[i] | right.Data()[i]; }
}
template <typename T>
void ShiftLeft(LocalTensor<T> dst, LocalTensor<T> src, uint32_t shift, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) { dst.Data()[i] = src.Data()[i] << shift; }
}
template <typename T>
void ShiftRight(LocalTensor<T> dst, LocalTensor<T> src, uint32_t shift, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) { dst.Data()[i] = src.Data()[i] >> shift; }
}
} // namespace AscendC
#endif
