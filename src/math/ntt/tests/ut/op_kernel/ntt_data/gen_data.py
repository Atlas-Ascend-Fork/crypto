#!/usr/bin/env python3
# coding=utf-8
#
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.

from __future__ import annotations

import math
import os
from pathlib import Path
from typing import List, Sequence, Tuple

import numpy as np

DEFAULT_N = 256
DEFAULT_Q = 8380417
DEFAULT_G = 10
DEFAULT_BLOCK_DIM = 10
DEFAULT_SUB_BENCH = 64
DEFAULT_POLY_COUNT = 1
DEFAULT_SEED = 99
DEFAULT_LOGP = 0
MAX_FULL_N = 65536
MAX_SUB_N = 256
MAX_LAUNCH_BLOCK_DIM = 16
MAX_RNS_CHANNELS = 16
RNS_CHANNEL_BITS = 28
ALLOWED_SUB_BENCH = (16, 32, 64, 128)
INPUT_DIR = Path("./input")
OUTPUT_DIR = Path("./output")
DIGIT_BASE_BITS = 7
DIGIT_BASE = 1 << DIGIT_BASE_BITS
DIGIT_COUNT = 4
BAT_TILE_COUNT = DIGIT_COUNT * DIGIT_COUNT  # [out_chunk][in_chunk]

# Standalone NTT follows the paper's practical choice of fixing one dimension to 128
# whenever the sub-kernel cap permits. 65536 would require a 128x512 stage, which
# exceeds the current Cube sub-kernel cap (<=256), so we fall back to 256x256.
FOURSTEP_FACTOR_TABLE = {
    4096: (64, 64),
    8192: (128, 64),
    16384: (128, 128),
    32768: (128, 256),
    65536: (256, 256),
}


def get_env_int(name: str, default: int) -> int:
    raw = os.getenv(name)
    return default if raw is None or raw == "" else int(raw)


def get_env_int_alias(names: Sequence[str], default: int) -> int:
    for name in names:
        raw = os.getenv(name)
        if raw is not None and raw != "":
            return int(raw)
    return default


def parse_int_list_env(name: str) -> List[int]:
    raw = os.getenv(name)
    if raw is None or raw.strip() == "":
        return []
    return [int(part.strip(), 0) for part in raw.split(",") if part.strip()]


def is_power_of_two(v: int) -> bool:
    return v > 0 and (v & (v - 1)) == 0


def kernel_n(logical_n: int) -> int:
    return max(32, logical_n)


def choose_default_sub_bench(n: int) -> int:
    if n == 4096:
        return 128
    if n == 65536:
        return 32
    return 64


def mul_mod_scalar(a: int, b: int, mod: int) -> int:
    return (a * b) % mod


def pow_mod_scalar(base: int, exp: int, mod: int) -> int:
    result = 1
    base %= mod
    while exp > 0:
        if exp & 1:
            result = mul_mod_scalar(result, base, mod)
        base = mul_mod_scalar(base, base, mod)
        exp >>= 1
    return result


def is_prime_u32(n: int) -> bool:
    if n < 2:
        return False
    small_primes = (2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37)
    for p in small_primes:
        if n == p:
            return True
        if n % p == 0:
            return False

    d = n - 1
    s = 0
    while d % 2 == 0:
        s += 1
        d //= 2

    # Deterministic for 32-bit integers.
    for a in (2, 3, 5, 7):
        if a >= n:
            continue
        x = pow(a, d, n)
        if x == 1 or x == n - 1:
            continue
        for _ in range(s - 1):
            x = (x * x) % n
            if x == n - 1:
                break
        else:
            return False
    return True


def prime_factors(n: int) -> List[int]:
    factors: List[int] = []
    d = 2
    while d * d <= n:
        if n % d == 0:
            factors.append(d)
            while n % d == 0:
                n //= d
        d = 3 if d == 2 else d + 2
    if n > 1:
        factors.append(n)
    return factors


def primitive_root(q: int) -> int:
    factors = prime_factors(q - 1)
    g = 2
    while g < q:
        ok = True
        for p in factors:
            if pow(g, (q - 1) // p, q) == 1:
                ok = False
                break
        if ok:
            return g
        g += 1
    raise ValueError(f"failed to find primitive root for q={q}")


def find_rns_ntt_primes(n: int, channels: int) -> Tuple[List[int], List[int]]:
    if channels > MAX_RNS_CHANNELS:
        raise ValueError(f"RNS channel count must be <= {MAX_RNS_CHANNELS}, got {channels}")
    upper = 1 << RNS_CHANNEL_BITS
    lower = 1 << (RNS_CHANNEL_BITS - 1)
    k = (upper - 2) // n
    qs: List[int] = []
    gs: List[int] = []
    while k > 0 and len(qs) < channels:
        q = k * n + 1
        if q < lower:
            break
        if is_prime_u32(q):
            qs.append(q)
            gs.append(primitive_root(q))
        k -= 1
    if len(qs) != channels:
        raise ValueError(
            f"could only find {len(qs)} {RNS_CHANNEL_BITS}-bit NTT primes for N={n}, need {channels}"
        )
    return qs, gs


def build_rns_moduli(n: int, logp: int, requested_channels: int) -> Tuple[List[int], List[int], int]:
    q_list = parse_int_list_env("NTT_Q_LIST")
    g_list = parse_int_list_env("NTT_G_LIST")
    if q_list:
        channels = len(q_list)
        if requested_channels > 0 and requested_channels != channels:
            raise ValueError("NTT_RNS_CHANNELS must match NTT_Q_LIST length")
        if channels > MAX_RNS_CHANNELS:
            raise ValueError(f"NTT_Q_LIST has {channels} channels, max is {MAX_RNS_CHANNELS}")
        if not g_list:
            g_list = [primitive_root(q) for q in q_list]
        if len(g_list) != channels:
            raise ValueError("NTT_G_LIST must have the same length as NTT_Q_LIST")
        return q_list, g_list, logp

    if logp > 0 or requested_channels > 1:
        channels = requested_channels if requested_channels > 0 else math.ceil(logp / RNS_CHANNEL_BITS)
        if channels <= 0:
            channels = 1
        qs, gs = find_rns_ntt_primes(n, channels)
        return qs, gs, logp

    return [get_env_int("NTT_Q", DEFAULT_Q)], [get_env_int("NTT_G", DEFAULT_G)], logp


def primitive_nth_root(q: int, g: int, n: int) -> int:
    return pow_mod_scalar(g, (q - 1) // n, q)


def generate_zetas(g: int, n: int, q: int) -> List[int]:
    omega = primitive_nth_root(q, g, n)
    zetas = [0] * n
    zetas[0] = 1
    idx = 1
    length = n // 2
    while length >= 1:
        w_base = pow_mod_scalar(omega, n // (2 * length), q)
        w = 1
        for _ in range(n // (2 * length)):
            if idx < n:
                zetas[idx] = w
                idx += 1
            w = mul_mod_scalar(w, w_base, q)
        length //= 2
    return zetas


def ntt_forward(coeffs: Sequence[int], n: int, q: int, g: int) -> List[int]:
    data = [int(c) % q for c in coeffs]
    zetas = generate_zetas(g, n, q)
    zeta_index = 1
    length = n // 2
    while length >= 1:
        step = 2 * length
        for start in range(0, n, step):
            zeta = zetas[zeta_index]
            zeta_index += 1
            for j in range(start, start + length):
                a = data[j]
                b = data[j + length]
                t = (zeta * b) % q
                data[j] = (a + t) % q
                data[j + length] = (a - t + q) % q
        length //= 2
    return data


def ntt_forward_layout_invariant_3step(
    coeffs: Sequence[int], n: int, q: int, g: int, n1: int, n2: int
) -> List[int]:
    data = [int(c) % q for c in coeffs]
    omega_n = primitive_nth_root(q, g, n)

    matrix = [[data[i * n2 + j] for j in range(n2)] for i in range(n1)]
    for i in range(n1):
        matrix[i] = ntt_forward(matrix[i], n2, q, g)

    stage3_rows = [[0] * n1 for _ in range(n2)]
    for i in range(n1):
        row_omega = pow_mod_scalar(omega_n, i, q)
        w = 1
        for j in range(n2):
            stage3_rows[j][i] = (matrix[i][j] * w) % q
            w = (w * row_omega) % q

    result: List[int] = []
    for j in range(n2):
        result.extend(ntt_forward(stage3_rows[j], n1, q, g))
    return result


def choose_fourstep_factors(n: int, block_dim: int) -> Tuple[int, int]:
    if n in FOURSTEP_FACTOR_TABLE:
        return FOURSTEP_FACTOR_TABLE[n]

    sqrt_n = int(math.sqrt(n))
    for i in range(sqrt_n, 1, -1):
        if n % i != 0:
            continue
        other = n // i
        if i % block_dim == 0 or other % block_dim == 0:
            return i, other
    for i in range(sqrt_n, 1, -1):
        if n % i == 0:
            return i, n // i
    return 1, n


def should_use_four_step(n: int, block_dim: int) -> bool:
    if n > MAX_SUB_N:
        return True
    return n >= 64 and block_dim > 1


def build_ntt_matrix(n: int, q: int, g: int) -> np.ndarray:
    matrix = np.zeros((n, n), dtype=np.int32)
    for row in range(n):
        basis = np.zeros(n, dtype=np.int32)
        basis[row] = 1
        matrix[row, :] = np.array(ntt_forward(basis, n, q, g), dtype=np.int32)
    return matrix


def build_twiddle_phase12(n1: int, n2: int, q: int, g: int) -> np.ndarray:
    omega_n = primitive_nth_root(q, g, n1 * n2)
    twiddle = np.zeros((n1, n2), dtype=np.int32)
    for i in range(n1):
        row_omega = pow_mod_scalar(omega_n, i, q)
        w = 1
        for j in range(n2):
            twiddle[i, j] = w
            w = mul_mod_scalar(w, row_omega, q)
    return twiddle


def pad_matrix(matrix: np.ndarray, kernel_n_value: int) -> np.ndarray:
    padded = np.zeros((kernel_n_value, kernel_n_value), dtype=np.int32)
    padded[: matrix.shape[0], : matrix.shape[1]] = matrix
    return padded


def build_bat_dense_pack(matrix: np.ndarray, kernel_n_value: int, q: int) -> np.ndarray:
    """Compile the left matrix into BAT dense chunk tiles.

    Output layout is 16 contiguous int8 tiles in the order [out_chunk][in_chunk],
    where each tile has shape [kernel_n, kernel_n] and stores the 7-bit chunk of
    (matrix * 2^(7*in_chunk) mod q) for the requested output chunk.
    """
    padded = pad_matrix(matrix, kernel_n_value)
    shift_weights = [pow_mod_scalar(DIGIT_BASE, c, q) for c in range(DIGIT_COUNT)]
    tiles = np.zeros((BAT_TILE_COUNT, kernel_n_value, kernel_n_value), dtype=np.int8)

    for out_chunk in range(DIGIT_COUNT):
        for in_chunk in range(DIGIT_COUNT):
            tile_idx = out_chunk * DIGIT_COUNT + in_chunk
            shifted = (padded.astype(np.int64) * shift_weights[in_chunk]) % q
            chunk = ((shifted >> (DIGIT_BASE_BITS * out_chunk)) & (DIGIT_BASE - 1)).astype(np.int8)
            tiles[tile_idx, :, :] = chunk
    return tiles.reshape(-1).astype(np.int8)


def build_case(
    n: int,
    q_list: Sequence[int],
    g_list: Sequence[int],
    logp: int,
    block_dim: int,
    channel_block: int,
    sub_bench: int,
    poly_count: int,
    seed: int,
) -> dict:
    if not is_power_of_two(n) or n > MAX_FULL_N:
        raise ValueError(f"NTT_N must be a power of two and <= {MAX_FULL_N}, got {n}")
    if block_dim <= 0 or sub_bench <= 0 or poly_count <= 0:
        raise ValueError("NTT_BLOCK_DIM / NTT_W / NTT_BENCH must be positive")
    if channel_block not in (1, 2, 4):
        raise ValueError(f"NTT_CHANNEL_BLOCK must be one of [1, 2, 4], got {channel_block}")
    active_channel_block = min(channel_block, len(q_list))
    if block_dim * active_channel_block > MAX_LAUNCH_BLOCK_DIM:
        raise ValueError(
            f"NTT_BLOCK_DIM * active channel block must be <= {MAX_LAUNCH_BLOCK_DIM}, "
            f"got {block_dim} * {active_channel_block}"
        )
    if sub_bench not in ALLOWED_SUB_BENCH:
        raise ValueError(f"NTT_W must be one of {list(ALLOWED_SUB_BENCH)}, got {sub_bench}")
    if n == 65536 and sub_bench > 32:
        raise ValueError("NTT_W must be <= 32 for NTT_N=65536 due to UB capacity")
    if len(q_list) != len(g_list):
        raise ValueError("q_list and g_list must have the same length")
    if len(q_list) <= 0 or len(q_list) > MAX_RNS_CHANNELS:
        raise ValueError(f"RNS channel count must be 1..{MAX_RNS_CHANNELS}, got {len(q_list)}")
    for channel, q in enumerate(q_list):
        if q <= 1 or q >= (1 << RNS_CHANNEL_BITS):
            raise ValueError(f"q[{channel}] must be < 2^{RNS_CHANNEL_BITS}, got {q}")
        if (q - 1) % n != 0:
            raise ValueError(f"q[{channel}]={q} does not satisfy q = 1 mod N={n}")

    use_four_step = should_use_four_step(n, block_dim)
    n1, n2 = choose_fourstep_factors(n, block_dim) if use_four_step else (1, n)
    if use_four_step and (n1 > MAX_SUB_N or n2 > MAX_SUB_N):
        raise ValueError(f"four-step factorization must keep sub-NTT <= {MAX_SUB_N}, got n1={n1}, n2={n2}")
    phase1_logical_n = n2 if use_four_step else n
    phase2_logical_n = n1 if use_four_step else n
    phase1_kernel_n = kernel_n(phase1_logical_n)
    phase2_kernel_n = kernel_n(phase2_logical_n)
    if phase1_kernel_n > MAX_SUB_N or phase2_kernel_n > MAX_SUB_N:
        raise ValueError(
            f"stage kernel N must be <= {MAX_SUB_N}, got phase1={phase1_kernel_n}, phase2={phase2_kernel_n}"
        )

    rng = np.random.default_rng(seed)
    input_parts = []
    golden_parts = []
    phase1_parts = []
    phase2_parts = []
    twiddle_parts = []

    for q, g in zip(q_list, g_list):
        polys = [rng.integers(0, q, size=n, dtype=np.int64).astype(np.int32) for _ in range(poly_count)]
        input_parts.append(np.concatenate(polys).astype(np.int32))

        for poly in polys:
            if use_four_step:
                golden_parts.append(
                    np.array(ntt_forward_layout_invariant_3step(poly, n, q, g, n1, n2), dtype=np.int32)
                )
            else:
                golden_parts.append(np.array(ntt_forward(poly, n, q, g), dtype=np.int32))

        phase1_parts.append(build_bat_dense_pack(build_ntt_matrix(phase1_logical_n, q, g), phase1_kernel_n, q))
        phase2_parts.append(build_bat_dense_pack(build_ntt_matrix(phase2_logical_n, q, g), phase2_kernel_n, q))
        twiddle_parts.append(
            build_twiddle_phase12(n1, n2, q, g).astype(np.int32)
            if use_four_step
            else np.ones((1,), dtype=np.int32)
        )

    packed_input = np.concatenate(input_parts).astype(np.int32)
    golden = np.concatenate(golden_parts).astype(np.int32)
    phase1_matrix = np.concatenate(phase1_parts).astype(np.int8)
    phase2_matrix = np.concatenate(phase2_parts).astype(np.int8)
    twiddle_phase12 = np.concatenate(twiddle_parts).astype(np.int32)

    INPUT_DIR.mkdir(parents=True, exist_ok=True)
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    q_pad = list(q_list) + [0] * (MAX_RNS_CHANNELS - len(q_list))
    g_pad = list(g_list) + [0] * (MAX_RNS_CHANNELS - len(g_list))
    tiling_values = [
        n,
        int(q_list[0]),
        int(g_list[0]),
        poly_count,
        block_dim,
        sub_bench,
        n1,
        n2,
        1 if use_four_step else 0,
        phase1_kernel_n,
        phase2_kernel_n,
        len(q_list),
        logp,
        *q_pad,
        *g_pad,
        channel_block,
    ]
    np.array(tiling_values, dtype=np.int32).tofile(INPUT_DIR / "input_tiling.bin")
    packed_input.tofile(INPUT_DIR / "input_x.bin")
    phase1_matrix.tofile(INPUT_DIR / "M_phase1.bin")
    phase2_matrix.tofile(INPUT_DIR / "M_phase2.bin")
    twiddle_phase12.tofile(INPUT_DIR / "twiddle_phase12.bin")
    golden.tofile(OUTPUT_DIR / "golden.bin")

    return {
        "n": n,
        "rns_channels": len(q_list),
        "channel_block": min(channel_block, len(q_list)),
        "logp": logp,
        "q": list(q_list),
        "g": list(g_list),
        "block_dim": block_dim,
        "sub_bench": sub_bench,
        "poly_count": poly_count,
        "use_four_step": 1 if use_four_step else 0,
        "n1": n1,
        "n2": n2,
        "phase1_kernel_n": phase1_kernel_n,
        "phase2_kernel_n": phase2_kernel_n,
        "bat_tiles_per_phase": BAT_TILE_COUNT,
        "seed": seed,
    }


def main() -> None:
    n = get_env_int("NTT_N", DEFAULT_N)
    raw_sub_bench = os.getenv("NTT_W")
    if raw_sub_bench is None or raw_sub_bench == "":
        raw_sub_bench = os.getenv("NTT_SUB_BENCH")
    sub_bench = int(raw_sub_bench) if raw_sub_bench not in (None, "") else choose_default_sub_bench(n)
    logp = get_env_int("NTT_LOGP", DEFAULT_LOGP)
    requested_channels = get_env_int("NTT_RNS_CHANNELS", 0)
    q_list, g_list, effective_logp = build_rns_moduli(n, logp, requested_channels)

    meta = build_case(
        n=n,
        q_list=q_list,
        g_list=g_list,
        logp=effective_logp,
        block_dim=get_env_int_alias(("NTT_BLOCK_DIM", "NTT_MAX_ACTIVE_CORES"), DEFAULT_BLOCK_DIM),
        channel_block=get_env_int("NTT_CHANNEL_BLOCK", 1),
        sub_bench=sub_bench,
        poly_count=get_env_int_alias(("NTT_BENCH", "NTT_POLY_COUNT"), DEFAULT_POLY_COUNT),
        seed=get_env_int("NTT_SEED", DEFAULT_SEED),
    )
    print(meta)
    print("Generated input_tiling.bin, input_x.bin, M_phase1.bin, M_phase2.bin, twiddle_phase12.bin, golden.bin")


if __name__ == "__main__":
    main()
