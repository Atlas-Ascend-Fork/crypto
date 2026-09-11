#pragma once

#include <stdint.h>

struct Shake256TensorTilingData {
    uint32_t batch;
    uint32_t inputLength;
    uint32_t outputLength;
    uint32_t blockDim;
    uint32_t groupSize;
    uint32_t useFastPath;
    uint32_t reserved0;
    uint32_t reserved1;
};
