#ifndef CANN_CRYPTO_SHA3_REF_H
#define CANN_CRYPTO_SHA3_REF_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
/* Keccak 与 SHAKE 的独立测试参考实现。 */
uint64_t CannCryptoRefLoad64Le(const uint8_t *input);
void CannCryptoRefStore64Le(uint8_t *output, uint64_t value);
void CannCryptoRefKeccakF1600(uint64_t state[25]);
int CannCryptoRefShake(uint32_t algorithm, const uint8_t *input, size_t inputLength,
    uint8_t *output, size_t outputLength);
#ifdef __cplusplus
}
#endif
#endif
