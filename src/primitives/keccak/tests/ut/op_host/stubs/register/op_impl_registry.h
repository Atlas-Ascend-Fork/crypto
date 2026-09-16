/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_CRYPTO_TEST_OP_IMPL_REGISTRY_H
#define CANN_CRYPTO_TEST_OP_IMPL_REGISTRY_H

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vector>
#include <array>

// Host 单元测试使用的上下文接口替身，不模拟 CANN 运行时。
namespace ge {
using graphStatus = int;
constexpr graphStatus GRAPH_SUCCESS = 0;
constexpr graphStatus GRAPH_FAILED = 1;
} // namespace ge

namespace gert {
class Shape {
public:
    Shape(std::initializer_list<int64_t> dims = {}) : dims_(dims) {}
    size_t GetDimNum() const { return dims_.size(); }
    int64_t GetDim(size_t index) const { return dims_.at(index); }
    void SetDimNum(size_t count) { dims_.resize(count); }
    void SetDim(size_t index, int64_t value) { dims_.at(index) = value; }

private:
    std::vector<int64_t> dims_;
};

class InferShapeContext {
public:
    InferShapeContext(const Shape* input, Shape* output) : input_(input), output_(output) {}
    const Shape* GetInputShape(size_t) const { return input_; }
    Shape* GetOutputShape(size_t) const { return output_; }

private:
    const Shape* input_;
    Shape* output_;
};

class StorageShape {
public:
    StorageShape(std::initializer_list<int64_t> dims) : shape_(dims) {}
    const Shape& GetStorageShape() const { return shape_; }

private:
    Shape shape_;
};

struct TestPlatform {
    uint32_t coreCount = 20;
};

struct RawTilingData {
    void SetDataSize(size_t value) { size = value; }
    size_t size = 0;
};

class TilingContext {
public:
    TilingContext(const StorageShape* x, const StorageShape* y) : input(x), output(y) {}
    const StorageShape* GetInputShape(size_t) const { return input; }
    const StorageShape* GetOutputShape(size_t) const { return output; }
    TestPlatform* GetPlatformInfo() { return platform; }
    template <typename T>
    T* GetTilingData()
    {
        static_assert(sizeof(T) <= 128, "Tiling test buffer is too small");
        return hasTiling ? reinterpret_cast<T*>(data.data()) : nullptr;
    }
    RawTilingData* GetRawTilingData() { return hasRaw ? &raw : nullptr; }
    size_t* GetWorkspaceSizes(size_t) { return hasWorkspace ? &workspace : nullptr; }
    void SetBlockDim(uint32_t value) { blockDim = value; }
    void SetTilingKey(uint64_t value) { tilingKey = value; }

    const StorageShape* input;
    const StorageShape* output;
    TestPlatform device;
    TestPlatform* platform = &device;
    bool hasTiling = true;
    bool hasRaw = true;
    bool hasWorkspace = true;
    uint32_t blockDim = 0;
    uint64_t tilingKey = UINT64_MAX;
    size_t workspace = SIZE_MAX;
    RawTilingData raw;

private:
    alignas(std::max_align_t) std::array<unsigned char, 128> data{};
};
} // namespace gert

struct TestInferRegistration {
    template <typename Function>
    TestInferRegistration& InferShape(Function)
    {
        return *this;
    }
    template <typename Function>
    TestInferRegistration& Tiling(Function)
    {
        return *this;
    }
};
#define IMPL_OP_INFERSHAPE(name) [[maybe_unused]] static auto test_registration_##name = TestInferRegistration()
#define IMPL_OP_OPTILING(name) [[maybe_unused]] static auto test_tiling_registration_##name = TestInferRegistration()
#endif
