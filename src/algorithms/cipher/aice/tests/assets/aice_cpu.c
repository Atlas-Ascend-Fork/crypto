/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#include "aice_api_cpu.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h> /* memcpy / memset */

#if defined(__cplusplus) || !defined(__STDC_VERSION__) || __STDC_VERSION__ < 199901L
#ifdef __GNUC__
#define restrict __restrict__
#elif defined(_MSC_VER)
#define restrict __restrict
#else
#define restrict
#endif
#endif

static const uint16_t Const[13] = {0x243F, 0x6A88, 0x85A3, 0x08D3, 0x1319, 0x8A2E, 0x0370,
                                   0x7344, 0xA409, 0x3822, 0x299F, 0x31D0, 0x082E};
static const int RC[37] = {11, 15, 15, 7,  12, 2, 13, 1, 6, 8,  1,  5, 14, 12, 10, 9,  5,  15, 4,
                           7,  12, 11, 15, 13, 7, 4,  8, 1, 11, 14, 9, 8,  3,  5,  14, 10, 10};

/* ---------- AVX2 ---------- */
#if defined(__AVX2__)
#include <immintrin.h>
#include <wmmintrin.h>
typedef __m256i DATAb;
#define BLOCK_SIZE 16

#define SIMD_ALIGN 32
#define SIMD_LOAD(p) _mm256_load_si256((const __m256i*)(p))
#define SIMD_STORE(p, v) _mm256_store_si256((__m256i*)(p), (v))
#define SIMD_LOADU(p) _mm256_loadu_si256((const __m256i*)(p))
#define SIMD_STOREU(p, v) _mm256_storeu_si256((__m256i*)(p), (v))
#define SIMD_SET_16(x) _mm256_set1_epi16((int16_t)(x))
#define SIMD_COUNTER_16() _mm256_setr_epi16(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15)
#define SIMD_ADD_16(a, b) _mm256_add_epi16((a), (b))
#define SIMD_SUB_16(a, b) _mm256_sub_epi16((a), (b))
#define SIMD_XOR_16(a, b) _mm256_xor_si256((a), (b))
#define SIMD_OR_16(a, b) _mm256_or_si256((a), (b))
#define SIMD_MUL_16(a, b) _mm256_mullo_epi16((a), (b))
#define SIMD_MULADD_16(a, b, c) _mm256_add_epi16((a), _mm256_mullo_epi16((b), (c)))
#define SIMD_ROTL_16(x, n) _mm256_or_si256(_mm256_slli_epi16((x), (n)), _mm256_srli_epi16((x), 16 - (n)))
#define PREFETCH(p) _mm_prefetch((const char*)(p), _MM_HINT_T0)

/* ---------- ARM NEON ---------- */
#elif defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__)
#include <arm_neon.h>
typedef uint16x8_t DATAb;
#define BLOCK_SIZE 8

#define SIMD_ALIGN 16
#define SIMD_LOAD(p) vld1q_u16((const uint16_t*)(p))
#define SIMD_STORE(p, v) vst1q_u16((uint16_t*)(p), (v))
#define SIMD_LOADU(p) vld1q_u16((const uint16_t*)(p))
#define SIMD_STOREU(p, v) vst1q_u16((uint16_t*)(p), (v))
#define SIMD_SET_16(x) vdupq_n_u16((uint16_t)(x))
#define SIMD_COUNTER_16() ((uint16x8_t){0, 1, 2, 3, 4, 5, 6, 7})
#define SIMD_ADD_16(a, b) vaddq_u16((a), (b))
#define SIMD_SUB_16(a, b) vsubq_u16((a), (b))
#define SIMD_XOR_16(a, b) veorq_u16((a), (b))
#define SIMD_OR_16(a, b) vorrq_u16((a), (b))
#define SIMD_MUL_16(a, b) vmulq_u16((a), (b))
/* NEON: single-instruction fused multiply-accumulate */
#define SIMD_MULADD_16(a, b, c) vmlaq_u16((a), (b), (c))
#define SIMD_ROTL_16(x, n) \
    vorrq_u16(vshlq_u16((x), vdupq_n_s16((int16_t)(n))), vshlq_u16((x), vdupq_n_s16(-(int16_t)(16 - (n)))))
#define PREFETCH(p) __builtin_prefetch((p))

/* ---------- x86 SSE2 (no AVX2) ---------- */
#elif defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#include <wmmintrin.h>
typedef __m128i DATAb;
#define BLOCK_SIZE 8

#define SIMD_ALIGN 16
#define SIMD_LOAD(p) _mm_load_si128((const __m128i*)(p))
#define SIMD_STORE(p, v) _mm_store_si128((__m128i*)(p), (v))
#define SIMD_LOADU(p) _mm_loadu_si128((const __m128i*)(p))
#define SIMD_STOREU(p, v) _mm_storeu_si128((__m128i*)(p), (v))
#define SIMD_SET_16(x) _mm_set1_epi16((int16_t)(x))
#define SIMD_COUNTER_16() _mm_setr_epi16(0, 1, 2, 3, 4, 5, 6, 7)
#define SIMD_ADD_16(a, b) _mm_add_epi16((a), (b))
#define SIMD_SUB_16(a, b) _mm_sub_epi16((a), (b))
#define SIMD_XOR_16(a, b) _mm_xor_si128((a), (b))
#define SIMD_OR_16(a, b) _mm_or_si128((a), (b))
#define SIMD_MUL_16(a, b) _mm_mullo_epi16((a), (b))
#define SIMD_MULADD_16(a, b, c) _mm_add_epi16((a), _mm_mullo_epi16((b), (c)))
#define SIMD_ROTL_16(x, n) _mm_or_si128(_mm_slli_epi16((x), (n)), _mm_srli_epi16((x), 16 - (n)))
#define PREFETCH(p) _mm_prefetch((const char*)(p), _MM_HINT_T0)

#else
#error "Unsupported architecture"
#endif

#define S (BATCH / BLOCK_SIZE)
#define BLOCK_BYTES (32 * BATCH * (size_t)sizeof(uint16_t))

typedef struct __attribute__((aligned(64))) {
    DATAb s[STATE][S];
    int head;
} ai_state_t;

typedef struct __attribute__((aligned(64))) {
    DATAb t[32][S];
    int head; /* reserved, kept for ABI compatibility */
} ai_table_t;

/* Compile-time assertion: each row must be SIMD_ALIGN-aligned.
   Row size = S * sizeof(DATAb). */
typedef char _row_align_check[((S * sizeof(DATAb)) % SIMD_ALIGN == 0) ? 1 : -1];

/* ============================================================
   CORE INLINE HELPERS
   ============================================================ */

static inline int idx(int h, int off)
{
    int x = h + off;
    return (x >= STATE) ? x - STATE : x;
}

/* Z[b] = ((S[1] + S[5] + S[19] + S[32]) mod 2^16) XOR S[30]    -- one column */
static inline DATAb compute_Z(const ai_state_t* st, int h, int b)
{
    DATAb r1 = SIMD_LOAD(&st->s[idx(h, 1)][b]);
    DATAb r5 = SIMD_LOAD(&st->s[idx(h, 5)][b]);
    DATAb r19 = SIMD_LOAD(&st->s[idx(h, 19)][b]);
    DATAb r30 = SIMD_LOAD(&st->s[idx(h, 30)][b]);
    DATAb r32 = SIMD_LOAD(&st->s[idx(h, 32)][b]);
    return SIMD_XOR_16(r30, SIMD_ADD_16(SIMD_ADD_16(r1, r5), SIMD_ADD_16(r19, r32)));
}

/* new = (rotl16(S[0] + ((S[15]|1)*S[25]), y) XOR S[22]) + S[17]
   Result lands at row h; after advance_head it becomes the new S[36].
   Uses SIMD_MULADD_16 so ARM NEON fuses the add+mul into vmla. */
static inline void shift_col(ai_state_t* st, int h, int b, int y)
{
    DATAb v0 = SIMD_LOAD(&st->s[h][b]);
    DATAb v15 = SIMD_LOAD(&st->s[idx(h, 15)][b]);
    DATAb v17 = SIMD_LOAD(&st->s[idx(h, 17)][b]);
    DATAb v22 = SIMD_LOAD(&st->s[idx(h, 22)][b]);
    DATAb v25 = SIMD_LOAD(&st->s[idx(h, 25)][b]);
    DATAb t = SIMD_MULADD_16(v0, v25, SIMD_OR_16(v15, SIMD_SET_16(1)));
    t = SIMD_ROTL_16(t, y);
    t = SIMD_XOR_16(t, v22);
    t = SIMD_ADD_16(t, v17);
    SIMD_STORE(&st->s[h][b], t);
}

static inline void advance_head(ai_state_t* st)
{
    int h = st->head + 1;
    st->head = (h == STATE) ? 0 : h;
}

/* One full state-update round, no output. */
static inline void state_round(ai_state_t* st, int rot)
{
    int h = st->head;
    int y = RC[rot];
    for (int b = 0; b < S; b++)
        shift_col(st, h, b, y);
    advance_head(st);
}

/* ============================================================
   M-TABLE INITIALISATION
   32 hidden steps emit Z into table->t[j], then 5 gap rounds.
   ============================================================ */

void ai_stream_table_init(ai_state_t* restrict st, ai_table_t* restrict table)
{
    for (int j = 0; j < 32; j++) {
        int h = st->head;
        int y = RC[j]; /* j < 37, so j % 37 == j */
        for (int b = 0; b < S; b++) {
            table->t[j][b] = compute_Z(st, h, b);
            shift_col(st, h, b, y);
        }
        advance_head(st);
    }
    for (int g = 0; g < 5; g++)
        state_round(st, 32 + g);
}

/* ============================================================
   INIT
   Order: load state -> 333 diffusion rounds -> key feedforward
          -> ai_stream_table_init (32 hidden + 5 gap).
   ============================================================ */

void ai_stream_init(
    ai_state_t* restrict st, ai_table_t* restrict table, const uint8_t* key, const uint8_t* iv, int offset)
{
    uint16_t iv16[6];
    uint16_t key16[16];
    /* memcpy avoids strict-aliasing & alignment UB on the caller buffer. */
    memcpy(iv16, iv, sizeof(iv16));
    memcpy(key16, key, sizeof(key16));

    st->head = 0;

    /* Initial state: nonce | key | const, broadcast across all SIMD lanes. */
    for (int b = 0; b < S; b++) {
        for (int i = 0; i < 6; i++)
            st->s[i][b] = SIMD_SET_16(iv16[i]);
        for (int i = 0; i < 16; i++)
            st->s[8 + i][b] = SIMD_SET_16(key16[i]);
        for (int i = 0; i < 13; i++)
            st->s[24 + i][b] = SIMD_SET_16(Const[i]);
    }

    /* lane_id = offset*BATCH + b*BLOCK_SIZE + lane_index.
       Encode its low/high 16 bits before the 333 diffusion updates.
       Caller must keep the complete lane range within uint32_t. */
    for (int b = 0; b < S; b++) {
        uint16_t low[BLOCK_SIZE], high[BLOCK_SIZE];
        for (unsigned j = 0; j < BLOCK_SIZE; ++j) {
            uint32_t lane_id = (uint32_t)((uint64_t)(unsigned)offset * BATCH + (uint64_t)b * BLOCK_SIZE + j);
            low[j] = (uint16_t)lane_id;
            high[j] = (uint16_t)(lane_id >> 16);
        }
        st->s[6][b] = SIMD_LOADU(low);
        st->s[7][b] = SIMD_LOADU(high);
    }

    /* 333 diffusion rounds. */
    for (int r = 0; r < 333; r++)
        state_round(st, r % 37);

    /* Key feedforward (immediately after round 333, before M is generated). */
    {
        int h = st->head;
        for (int b = 0; b < S; b++) {
            for (int i = 0; i < 16; i++) {
                int a = idx(h, i);
                st->s[a][b] = SIMD_ADD_16(st->s[a][b], SIMD_SET_16(key16[i]));
            }
            for (int i = 0; i < 16; i++) {
                int a = idx(h, 16 + i);
                st->s[a][b] = SIMD_ADD_16(st->s[a][b], SIMD_SET_16(key16[i]));
            }
            for (int i = 0; i < 5; i++) {
                int a = idx(h, 32 + i);
                st->s[a][b] = SIMD_ADD_16(st->s[a][b], SIMD_SET_16(key16[i]));
            }
        }
    }

    /* Hidden 37-step cycle: 32 steps fill M[0..31] with secret keystream,
       followed by 5 gap rounds (no output). */
    ai_stream_table_init(st, table);
}

/* ============================================================
   STREAM ENCRYPT / DECRYPT
   Public output (per step j):
     Z   = intermediate_output(S)
     C   = (Z + M_prev) mod 2^16            ; keystream word
     M_j = Z                                 ; update memory
   encrypt:  out = in + C       (= in + M_prev + Z)
   decrypt:  out = in - C       (= in - M_prev - Z)

   `src` and `dst` may alias (in-place); no restrict on them.
   ============================================================ */

void ai_stream_encrypt(
    ai_state_t* restrict st, ai_table_t* restrict table, const uint8_t* src, uint8_t* dst, size_t size)
{
    const uint16_t* src16 = (const uint16_t*)src;
    uint16_t* dst16 = (uint16_t*)dst;
    const size_t blocks = size / BLOCK_BYTES;

    for (size_t i = 0; i < blocks; i++) {
        for (int j = 0; j < 32; j++) {
            int h = st->head;
            int y = RC[j];
            const uint16_t* in = &src16[(32 * i + j) * BATCH];
            uint16_t* out = &dst16[(32 * i + j) * BATCH];
            PREFETCH(in + BATCH);

            for (int b = 0; b < S; b++) {
                DATAb p = SIMD_LOADU(&in[BLOCK_SIZE * b]);
                DATAb z_new = compute_Z(st, h, b);
                DATAb m_old = table->t[j][b];
                DATAb c = SIMD_ADD_16(p, SIMD_ADD_16(m_old, z_new));
                SIMD_STOREU(&out[BLOCK_SIZE * b], c);
                table->t[j][b] = z_new;
                shift_col(st, h, b, y);
            }
            advance_head(st);
        }
        for (int g = 0; g < 5; g++)
            state_round(st, 32 + g);
    }
}

void ai_stream_decrypt(
    ai_state_t* restrict st, ai_table_t* restrict table, const uint8_t* src, uint8_t* dst, size_t size)
{
    const uint16_t* src16 = (const uint16_t*)src;
    uint16_t* dst16 = (uint16_t*)dst;
    const size_t blocks = size / BLOCK_BYTES;

    for (size_t i = 0; i < blocks; i++) {
        for (int j = 0; j < 32; j++) {
            int h = st->head;
            int y = RC[j];
            const uint16_t* in = &src16[(32 * i + j) * BATCH];
            uint16_t* out = &dst16[(32 * i + j) * BATCH];
            PREFETCH(in + BATCH);

            for (int b = 0; b < S; b++) {
                DATAb c = SIMD_LOADU(&in[BLOCK_SIZE * b]);
                DATAb z_new = compute_Z(st, h, b);
                DATAb m_old = table->t[j][b];
                DATAb p = SIMD_SUB_16(SIMD_SUB_16(c, m_old), z_new);
                SIMD_STOREU(&out[BLOCK_SIZE * b], p);
                table->t[j][b] = z_new;
                shift_col(st, h, b, y);
            }
            advance_head(st);
        }
        for (int g = 0; g < 5; g++)
            state_round(st, 32 + g);
    }
}

/* ============================================================
   PUBLIC API
   ============================================================ */

void aice_encrypt_cpu(
    const uint8_t* key, const uint8_t* iv, const uint8_t* plaintext, uint8_t* ciphertext, const size_t msg_len)
{
    const size_t blocks = msg_len / BLOCK_BYTES;
    const size_t size = blocks * BLOCK_BYTES;
    const size_t left = msg_len - size;

    ai_state_t state;
    ai_table_t table;

    ai_stream_init(&state, &table, key, iv, 0);
    ai_stream_encrypt(&state, &table, plaintext, ciphertext, size);

    if (left > 0) {
        uint8_t buffer[BLOCK_BYTES];
        memset(buffer, 0, sizeof(buffer));
        memcpy(buffer, plaintext + size, left);
        ai_stream_encrypt(&state, &table, buffer, buffer, sizeof(buffer));
        memcpy(ciphertext + size, buffer, left);
    }
}

void aice_decrypt_cpu(
    const uint8_t* key, const uint8_t* iv, const uint8_t* ciphertext, uint8_t* plaintext, const size_t msg_len)
{
    const size_t blocks = msg_len / BLOCK_BYTES;
    const size_t size = blocks * BLOCK_BYTES;
    const size_t left = msg_len - size;

    ai_state_t state;
    ai_table_t table;

    ai_stream_init(&state, &table, key, iv, 0);
    ai_stream_decrypt(&state, &table, ciphertext, plaintext, size);

    if (left > 0) {
        uint8_t buffer[BLOCK_BYTES];
        memset(buffer, 0, sizeof(buffer));
        memcpy(buffer, ciphertext + size, left);
        ai_stream_decrypt(&state, &table, buffer, buffer, sizeof(buffer));
        memcpy(plaintext + size, buffer, left);
    }
}
