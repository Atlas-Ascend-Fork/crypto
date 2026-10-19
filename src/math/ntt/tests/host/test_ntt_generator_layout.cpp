/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ntt_internal.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

int main(int argc, char** argv)
{
    if (argc != 2) {
        return 1;
    }
    const auto input = std::filesystem::path(argv[1]) / "input";
    NttTilingData generated{};
    std::ifstream metadata(input / "input_tiling.bin", std::ios::binary);
    metadata.read(reinterpret_cast<char*>(&generated), sizeof(generated));
    if (!metadata) {
        return 1;
    }
    cann_crypto::ntt::NttParams params;
    params.n = generated.n;
    params.q = generated.q;
    params.g = generated.g;
    params.blockDim = generated.blockDim;
    params.subBench = generated.subBench;
    params.channelBlock = generated.channelBlock;
    params.logP = generated.logP;
    params.rnsChannels = generated.rnsChannels;
    params.qListCount = generated.rnsChannels;
    params.gListCount = generated.rnsChannels;
    for (int32_t channel = 0; channel < generated.rnsChannels; ++channel) {
        params.qList[channel] = generated.qList[channel];
        params.gList[channel] = generated.gList[channel];
    }
    params.xElements = std::filesystem::file_size(input / "input_x.bin") / sizeof(int32_t);
    params.matrixPhase1Elements = std::filesystem::file_size(input / "M_phase1.bin");
    params.matrixPhase2Elements = std::filesystem::file_size(input / "M_phase2.bin");
    params.twiddleElements = std::filesystem::file_size(input / "twiddle_phase12.bin") / sizeof(int32_t);

    NttTilingData actual{};
    size_t workspaceBytes = 0;
    const char* error = nullptr;
    if (cann_crypto::ntt::BuildNttTiling(params, actual, workspaceBytes, &error) !=
        cann_crypto::ntt::NttStatus::Success) {
        std::cerr << error << '\n';
        return 1;
    }
    if (std::memcmp(&generated, &actual, sizeof(actual)) != 0) {
        std::cerr << "generated metadata differs from production Host tiling\n";
        return 1;
    }
    return 0;
}
