#include "sha3_ref.h"
#include <openssl/evp.h>
#include <cstdint>
#include <iostream>
#include <random>
#include <vector>

int main()
{
    std::mt19937 random(20260909U);
    size_t cases = 0;
    for (const uint32_t algorithm : {128U, 256U}) {
        const size_t rate = algorithm == 128U ? 168U : 136U;
        const std::vector<size_t> lengths{0, 1, 3, 4, 63, 64, rate - 1, rate, rate + 1,
            2 * rate - 1, 2 * rate, 2 * rate + 1, 1024, 4096};
        const std::vector<size_t> outputs{1, 3, 31, 32, 64, rate - 1, rate, rate + 1, 2 * rate + 3};
        for (const auto length : lengths) {
            std::vector<uint8_t> input(length == 0 ? 1 : length);
            for (auto &byte : input) {
                byte = static_cast<uint8_t>(random());
            }
            for (const auto outputLength : outputs) {
                std::vector<uint8_t> actual(outputLength);
                std::vector<uint8_t> expected(outputLength);
                auto *context = EVP_MD_CTX_new();
                if (context == nullptr) {
                    return 1;
                }
                const auto *method = algorithm == 128U ? EVP_shake128() : EVP_shake256();
                const bool ok = EVP_DigestInit_ex(context, method, nullptr) == 1 &&
                    EVP_DigestUpdate(context, input.data(), length) == 1 &&
                    EVP_DigestFinalXOF(context, expected.data(), expected.size()) == 1;
                EVP_MD_CTX_free(context);
                if (!ok || CannCryptoRefShake(algorithm, input.data(), length, actual.data(), actual.size()) != 0 ||
                    actual != expected) {
                    std::cerr << "differential mismatch: " << algorithm << ' ' << length << ' ' << outputLength << '\n';
                    return 1;
                }
                ++cases;
            }
        }
    }
    std::cout << "REFERENCE_OPENSSL_DIFF=PASS cases=" << cases << '\n';
    return 0;
}
