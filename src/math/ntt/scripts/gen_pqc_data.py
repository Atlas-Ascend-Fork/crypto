#!/usr/bin/env python3
# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

"""Generate deterministic inputs and independent goldens for PQC kernels."""

import hashlib
import logging
import os
import random
import struct
from pathlib import Path
from typing import NamedTuple

LOGGER = logging.getLogger(__name__)


class GeneratedCase(NamedTuple):
    input_a: list[int]
    input_b: list[int]
    golden: list[int]
    input_a_size: int
    input_b_size: int
    output_size: int


class ParityDigits(NamedTuple):
    parity: int
    output_digit: int
    input_digit: int


N = 256
MLKEM_K = 3
MLKEM_Q = 3329
MLKEM_QINV = 62209
MLKEM_ZETAS = (
    -1044, -758, -359, -1517, 1493, 1422, 287, 202,
    -171, 622, 1577, 182, 962, -1202, -1474, 1468,
    573, -1325, 264, 383, -829, 1458, -1602, -130,
    -681, 1017, 732, 608, -1542, 411, -205, -1571,
    1223, 652, -552, 1015, -1293, 1491, -282, -1544,
    516, -8, -320, -666, -1618, -1162, 126, 1469,
    -853, -90, -271, 830, 107, -1421, -247, -951,
    -398, 961, -1508, -725, 448, -1065, 677, -1275,
    -1103, 430, 555, 843, -1251, 871, 1550, 105,
    422, 587, 177, -235, -291, -460, 1574, 1653,
    -246, 778, 1159, -147, -777, 1483, -602, 1119,
    -1590, 644, -872, 349, 418, 329, -156, -75,
    817, 1097, 603, 610, 1322, -1285, -1465, 384,
    -1215, -136, 1218, -1335, -874, 220, -1187, -1659,
    -1185, -1530, -1278, 794, -1510, -854, -870, 478,
    -108, -308, 996, 991, 958, -1460, 1522, 1628,
)

MLDSA_K = 6
MLDSA_L = 5
MLDSA_Q = 8380417
MLDSA_QINV = 58728449

KERNEL_CODES = {
    "mlkem-poly": 0,
    "mldsa-poly": 1,
    "keccak": 2,
    "mlkem-sampling": 3,
    "slhdsa-hash": 4,
}
OPERATIONS = {
    "mlkem-poly": (
        "ntt", "intt", "roundtrip", "basemul", "matvec", "matvec3"
    ),
    "mldsa-poly": (
        "ntt", "intt", "roundtrip", "pointwise", "matvec"
    ),
    "keccak": ("shake128", "shake256", "sha3-256", "sha3-512"),
    "mlkem-sampling": ("cbd-eta2", "cbd-eta3", "rej-uniform"),
    "slhdsa-hash": ("wots-chain", "fors-leaf", "merkle-node"),
}
PROJECT_DIR = Path(os.getenv("NTT_DATA_DIR", Path.cwd()))
INPUT_DIR = PROJECT_DIR / "input"
OUTPUT_DIR = PROJECT_DIR / "output"
DIGIT_BASE_BITS = 7
DIGIT_BASE = 1 << DIGIT_BASE_BITS
DIGIT_COUNT = 4


def int16(value: int) -> int:
    value &= 0xFFFF
    return value - 0x10000 if value >= 0x8000 else value


def int32(value: int) -> int:
    value &= 0xFFFFFFFF
    return value - 0x100000000 if value >= 0x80000000 else value


def mlkem_montgomery_reduce(value: int) -> int:
    reduced = value & 0xFFFF
    inverted = (reduced * MLKEM_QINV) & 0xFFFF
    signed_inverted = int16(inverted)
    return int16((value - signed_inverted * MLKEM_Q) >> 16)


def mlkem_forward_ntt(coeffs: list[int]) -> list[int]:
    # Normal-domain, canonicalized optimization derived from
    # custom_op/include/ntt1.hpp.
    result = [value % MLKEM_Q for value in coeffs]
    for layer in range(1, 8):
        root_index = 1 << (layer - 1)
        length = N >> layer
        for start in range(0, N, 2 * length):
            root = mlkem_montgomery_reduce(
                MLKEM_ZETAS[root_index]
            ) % MLKEM_Q
            root_index += 1
            for j in range(start, start + length):
                product = result[j + length] * root % MLKEM_Q
                lhs = result[j]
                result[j] = (lhs + product) % MLKEM_Q
                result[j + length] = (lhs - product) % MLKEM_Q
    return result


def mlkem_inverse_ntt(coeffs: list[int]) -> list[int]:
    result = [(value % MLKEM_Q) * 512 % MLKEM_Q for value in coeffs]
    for layer in range(7, 0, -1):
        length = N >> layer
        root_index = (1 << layer) - 1
        for start in range(0, N, 2 * length):
            root = mlkem_montgomery_reduce(
                MLKEM_ZETAS[root_index]
            ) % MLKEM_Q
            root_index -= 1
            for j in range(start, start + length):
                lhs = result[j]
                rhs = result[j + length]
                result[j] = (lhs + rhs) % MLKEM_Q
                result[j + length] = (rhs - lhs) * root % MLKEM_Q
    return result


def build_transform_matrix(transform) -> list[list[int]]:
    """Build B so a row-vector polynomial is transformed by a @ B."""
    matrix: list[list[int]] = []
    for input_index in range(N):
        basis = [0] * N
        basis[input_index] = 1
        matrix.append(transform(basis))
    return matrix


def build_bat_dense_pack(
    matrix: list[list[int]], modulus: int
) -> bytes:
    """Use the exact 4x7-bit BAT layout consumed by ops-math NTT."""
    packed = bytearray()
    shift_weights = [
        pow(DIGIT_BASE, input_chunk, modulus)
        for input_chunk in range(DIGIT_COUNT)
    ]
    for output_chunk in range(DIGIT_COUNT):
        output_shift = DIGIT_BASE_BITS * output_chunk
        for input_chunk in range(DIGIT_COUNT):
            weight = shift_weights[input_chunk]
            for row in matrix:
                packed.extend(
                    ((value * weight) % modulus >> output_shift) &
                    (DIGIT_BASE - 1)
                    for value in row
                )
    return bytes(packed)


def centered_residue(value: int, modulus: int) -> int:
    value %= modulus
    return value - modulus if value > modulus // 2 else value


def balanced_int8_digits(value: int, count: int = 3) -> list[int]:
    digits: list[int] = []
    for _ in range(count):
        digit = (value + 128) % 256 - 128
        digits.append(digit)
        value = (value - digit) // 256
    if value != 0:
        raise RuntimeError("value does not fit balanced int8 digits")
    return digits


def build_mldsa_bat_dense_pack(
    matrix: list[list[int]], modulus: int
) -> bytes:
    """Pack a 3x3 signed base-256 BAT into the fixed 16-matrix buffer."""
    packed = bytearray()
    shift_weights = [pow(256, input_digit, modulus) for input_digit in range(3)]
    digit_matrices = [bytearray() for _ in range(9)]
    for output_digit in range(3):
        for input_digit, weight in enumerate(shift_weights):
            output = digit_matrices[output_digit * 3 + input_digit]
            for row in matrix:
                output.extend(
                    balanced_int8_digits(
                        centered_residue(value * weight, modulus)
                    )[output_digit] & 0xFF
                    for value in row
                )
    for digit_matrix in digit_matrices:
        packed.extend(digit_matrix)
    packed.extend(bytes((DIGIT_COUNT * DIGIT_COUNT - 9) * N * N))
    return bytes(packed)


def pack_mlkem_parity_digit(matrix, modulus, tile, digits):
    parity, output_digit, input_digit = digits
    weight = pow(DIGIT_BASE, input_digit, modulus)
    for input_slot in range(N // 2):
        input_index = 2 * input_slot + parity
        row = matrix[input_index]
        packed_row = input_digit * (N // 2) + input_slot
        row_offset = packed_row * N
        for output_slot in range(N // 2):
            output_index = 2 * output_slot + parity
            shifted = row[output_index] * weight % modulus
            tile[row_offset + output_slot] = (
                shifted >> (DIGIT_BASE_BITS * output_digit)
            ) & (DIGIT_BASE - 1)


def build_mlkem_parity_bat_dense_pack(
    matrix: list[list[int]], modulus: int
) -> bytes:
    """Pack two parity-isolated, two-digit BAT matrices.

    Each 256x256 matrix handles one coefficient parity.  Its K dimension is
    [input_digit0(128), input_digit1(128)] and its N dimension is
    [output_digit0(128), output_digit1(128)].  Kyber's seven-stage NTT never
    mixes even and odd coefficient indices, so the omitted cross-parity blocks
    are identically zero.
    """
    packed = bytearray()
    for parity in range(2):
        for output_digit in range(2):
            tile = bytearray(N * N)
            for input_digit in range(2):
                pack_mlkem_parity_digit(
                    matrix, modulus, tile, ParityDigits(parity, output_digit, input_digit)
                )
            packed.extend(tile)
    packed.extend(bytes((DIGIT_COUNT * DIGIT_COUNT - 4) * N * N))
    return bytes(packed)


def write_mlkem_bat_tables() -> None:
    forward = build_mlkem_parity_bat_dense_pack(
        build_transform_matrix(mlkem_forward_ntt), MLKEM_Q
    )
    inverse = build_mlkem_parity_bat_dense_pack(
        build_transform_matrix(mlkem_inverse_ntt), MLKEM_Q
    )
    expected_bytes = DIGIT_COUNT * DIGIT_COUNT * N * N
    if len(forward) != expected_bytes or len(inverse) != expected_bytes:
        raise RuntimeError("invalid ML-KEM BAT matrix size")
    (INPUT_DIR / "M_mlkem_forward.bin").write_bytes(forward)
    (INPUT_DIR / "M_mlkem_inverse.bin").write_bytes(inverse)


def mlkem_polyvec_basemul(lhs: list[int], rhs: list[int]) -> list[int]:
    result = [0] * N
    for i in range(N // 2):
        accumulator0 = 0
        accumulator1 = 0
        root = MLKEM_ZETAS[64 + (i >> 1)]
        gamma = root if (i & 1) == 0 else -root
        for vector_index in range(MLKEM_K):
            offset = vector_index * N + 2 * i
            a0, a1 = lhs[offset], lhs[offset + 1]
            b0, b1 = rhs[offset], rhs[offset + 1]
            cache = mlkem_montgomery_reduce(b1 * gamma)
            accumulator0 += a1 * cache + a0 * b0
            accumulator1 += a0 * b1 + a1 * b0
        result[2 * i] = mlkem_montgomery_reduce(accumulator0)
        result[2 * i + 1] = mlkem_montgomery_reduce(accumulator1)
    return result


def reverse_bits(value: int, width: int) -> int:
    result = 0
    for _ in range(width):
        result = (result << 1) | (value & 1)
        value >>= 1
    return result


def build_mldsa_zetas() -> tuple[int, ...]:
    """Derive the standard ML-DSA Montgomery zeta table locally."""
    montgomery_one = (1 << 32) % MLDSA_Q
    values = [0]
    for index in range(1, N):
        exponent = reverse_bits(index, 8)
        value = montgomery_one * pow(1753, exponent, MLDSA_Q) % MLDSA_Q
        values.append(centered_residue(value, MLDSA_Q))
    return tuple(values)


MLDSA_ZETAS = build_mldsa_zetas()


def mldsa_montgomery_reduce(value: int) -> int:
    factor = int32((value & 0xFFFFFFFF) * MLDSA_QINV)
    return int32((value - factor * MLDSA_Q) >> 32)


def mldsa_reduce32(value: int) -> int:
    quotient = (value + (1 << 22)) >> 23
    return int32(value - quotient * MLDSA_Q)


def mldsa_forward_ntt(coeffs: list[int]) -> list[int]:
    result = [int32(value) for value in coeffs]
    root_index = 0
    length = N // 2
    while length > 0:
        start = 0
        while start < N:
            root_index += 1
            root = MLDSA_ZETAS[root_index]
            end = start + length
            for j in range(start, end):
                product = mldsa_montgomery_reduce(
                    root * result[j + length]
                )
                lhs = result[j]
                result[j] = int32(lhs + product)
                result[j + length] = int32(lhs - product)
            start = end + length
        length >>= 1
    return result


def mldsa_inverse_ntt(coeffs: list[int]) -> list[int]:
    result = [int32(value) for value in coeffs]
    root_index = N
    length = 1
    while length < N:
        start = 0
        while start < N:
            root_index -= 1
            root = -MLDSA_ZETAS[root_index]
            end = start + length
            for j in range(start, end):
                lhs = result[j]
                rhs = result[j + length]
                result[j] = int32(lhs + rhs)
                result[j + length] = mldsa_montgomery_reduce(
                    root * int32(lhs - rhs)
                )
            start = end + length
        length <<= 1
    return [mldsa_montgomery_reduce(41978 * value) for value in result]


def mldsa_pointwise(lhs: list[int], rhs: list[int]) -> list[int]:
    return [
        mldsa_montgomery_reduce(a * b) for a, b in zip(lhs, rhs)
    ]


def write_mldsa_bat_tables() -> None:
    forward = build_mldsa_bat_dense_pack(
        build_transform_matrix(mldsa_forward_ntt), MLDSA_Q
    )
    inverse = build_mldsa_bat_dense_pack(
        build_transform_matrix(mldsa_inverse_ntt), MLDSA_Q
    )
    expected_bytes = DIGIT_COUNT * DIGIT_COUNT * N * N
    if len(forward) != expected_bytes or len(inverse) != expected_bytes:
        raise RuntimeError("invalid ML-DSA BAT matrix size")
    (INPUT_DIR / "M_mldsa_forward.bin").write_bytes(forward)
    (INPUT_DIR / "M_mldsa_inverse.bin").write_bytes(inverse)


def mlkem_ntt_case(polynomial, rng):
    input_a, input_b, golden = [], [], []
    input_a.extend(value % MLKEM_Q for value in polynomial)
    input_b.extend([0] * N)
    golden.extend(mlkem_forward_ntt(polynomial))
    return input_a, input_b, golden


def mlkem_intt_case(polynomial, rng):
    input_a, input_b, golden = [], [], []
    transformed = mlkem_forward_ntt(polynomial)
    input_a.extend(value % MLKEM_Q for value in transformed)
    input_b.extend([0] * N)
    golden.extend(mlkem_inverse_ntt(transformed))
    return input_a, input_b, golden


def mlkem_roundtrip_case(polynomial, rng):
    input_a, input_b, golden = [], [], []
    input_a.extend(value % MLKEM_Q for value in polynomial)
    input_b.extend([0] * N)
    golden.extend(
        mlkem_inverse_ntt(mlkem_forward_ntt(polynomial))
    )
    return input_a, input_b, golden


def mlkem_basemul_case(polynomial, rng):
    input_a, input_b, golden = [], [], []
    lhs = [rng.randrange(MLKEM_Q) for _ in range(MLKEM_K * N)]
    rhs: list[int] = []
    for _ in range(MLKEM_K):
        secret = [rng.randrange(-2, 3) for _ in range(N)]
        rhs.extend(mlkem_forward_ntt(secret))
    input_a.extend(lhs)
    input_b.extend(value % MLKEM_Q for value in rhs)
    golden.extend(mlkem_polyvec_basemul(lhs, rhs))
    return input_a, input_b, golden


def mlkem_matvec_case(polynomial, rng):
    input_a, input_b, golden = [], [], []
    lhs = [rng.randrange(MLKEM_Q) for _ in range(MLKEM_K * N)]
    rhs = [rng.randrange(-2, 3) for _ in range(MLKEM_K * N)]
    transformed_rhs: list[int] = []
    for column in range(MLKEM_K):
        start = column * N
        transformed_rhs.extend(
            mlkem_forward_ntt(rhs[start:start + N])
        )
    input_a.extend(lhs)
    input_b.extend(value % MLKEM_Q for value in rhs)
    golden.extend(
        mlkem_inverse_ntt(
            mlkem_polyvec_basemul(lhs, transformed_rhs)
        )
    )
    return input_a, input_b, golden


def mlkem_matvec3_case(polynomial, rng):
    input_a, input_b, golden = [], [], []
    lhs = [
        rng.randrange(MLKEM_Q)
        for _ in range(MLKEM_K * MLKEM_K * N)
    ]
    rhs = [rng.randrange(-2, 3) for _ in range(MLKEM_K * N)]
    transformed_rhs = []
    for column in range(MLKEM_K):
        start = column * N
        transformed_rhs.extend(
            mlkem_forward_ntt(rhs[start:start + N])
        )
    input_a.extend(lhs)
    input_b.extend(rhs)
    for row in range(MLKEM_K):
        start = row * MLKEM_K * N
        golden.extend(
            mlkem_inverse_ntt(
                mlkem_polyvec_basemul(
                    lhs[start:start + MLKEM_K * N],
                    transformed_rhs,
                )
            )
        )
    return input_a, input_b, golden


def generate_mlkem(operation: str, batch: int, rng: random.Random) -> GeneratedCase:
    input_a, input_b, golden = [], [], []
    generators = {
        "ntt": mlkem_ntt_case,
        "intt": mlkem_intt_case,
        "roundtrip": mlkem_roundtrip_case,
        "basemul": mlkem_basemul_case,
        "matvec": mlkem_matvec_case,
        "matvec3": mlkem_matvec3_case
    }
    generate_one = generators.get(operation)
    if generate_one is None:
        raise ValueError(f"Unsupported polynomial operation: {operation}")
    for _ in range(batch):
        polynomial = [rng.randrange(MLKEM_Q) for _ in range(N)]
        lhs, rhs, expected = generate_one(polynomial, rng)
        input_a.extend(lhs)
        input_b.extend(rhs)
        golden.extend(expected)
    input_a_size = (
        MLKEM_K * MLKEM_K * N if operation == "matvec3"
        else (MLKEM_K * N if operation in OPERATIONS["mlkem-poly"][3:]
              else N)
    )
    input_b_size = (
        MLKEM_K * N if operation in OPERATIONS["mlkem-poly"][3:] else N
    )
    output_size = MLKEM_K * N if operation == "matvec3" else N
    return GeneratedCase(
        input_a, input_b, golden,
        input_a_size, input_b_size, output_size,
    )


def mldsa_ntt_case(polynomial, rng):
    input_a, input_b, golden = [], [], []
    input_a.extend(polynomial)
    input_b.extend([0] * N)
    golden.extend(mldsa_forward_ntt(polynomial))
    return input_a, input_b, golden


def mldsa_intt_case(polynomial, rng):
    input_a, input_b, golden = [], [], []
    transformed = mldsa_forward_ntt(polynomial)
    input_a.extend(value % MLDSA_Q for value in transformed)
    input_b.extend([0] * N)
    golden.extend(mldsa_inverse_ntt(transformed))
    return input_a, input_b, golden


def mldsa_roundtrip_case(polynomial, rng):
    input_a, input_b, golden = [], [], []
    input_a.extend(polynomial)
    input_b.extend([0] * N)
    golden.extend(
        mldsa_inverse_ntt(mldsa_forward_ntt(polynomial))
    )
    return input_a, input_b, golden


def mldsa_pointwise_case(polynomial, rng):
    input_a, input_b, golden = [], [], []
    lhs = mldsa_forward_ntt(polynomial)
    rhs = mldsa_forward_ntt([
        rng.randrange(-(MLDSA_Q // 2), MLDSA_Q // 2)
        for _ in range(N)
    ])
    input_a.extend(lhs)
    input_b.extend(rhs)
    golden.extend(mldsa_pointwise(lhs, rhs))
    return input_a, input_b, golden


def mldsa_matvec_case(polynomial, rng):
    input_a, input_b, golden = [], [], []
    matrix = [
        rng.randrange(MLDSA_Q)
        for _ in range(MLDSA_K * MLDSA_L * N)
    ]
    vector = [
        rng.randrange(-4, 5) for _ in range(MLDSA_L * N)
    ]
    transformed_vector: list[int] = []
    for column in range(MLDSA_L):
        start = column * N
        transformed_vector.extend(
            mldsa_forward_ntt(vector[start:start + N])
        )
    input_a.extend(matrix)
    input_b.extend(value % MLDSA_Q for value in vector)
    for row in range(MLDSA_K):
        result = [0] * N
        for coefficient in range(N):
            accumulator = 0
            for column in range(MLDSA_L):
                matrix_offset = (
                    (row * MLDSA_L + column) * N + coefficient
                )
                vector_offset = column * N + coefficient
                accumulator = int32(
                    accumulator + mldsa_montgomery_reduce(
                        matrix[matrix_offset] *
                        transformed_vector[vector_offset]
                    )
                )
            result[coefficient] = mldsa_reduce32(accumulator)
        golden.extend(mldsa_inverse_ntt(result))
    return input_a, input_b, golden


def generate_mldsa(operation: str, batch: int, rng: random.Random) -> GeneratedCase:
    input_a, input_b, golden = [], [], []
    generators = {
        "ntt": mldsa_ntt_case,
        "intt": mldsa_intt_case,
        "roundtrip": mldsa_roundtrip_case,
        "pointwise": mldsa_pointwise_case,
        "matvec": mldsa_matvec_case
    }
    generate_one = generators.get(operation)
    if generate_one is None:
        raise ValueError(f"Unsupported polynomial operation: {operation}")
    for _ in range(batch):
        polynomial = [
            rng.randrange(-(MLDSA_Q // 2), MLDSA_Q // 2)
            for _ in range(N)
        ]
        lhs, rhs, expected = generate_one(polynomial, rng)
        input_a.extend(lhs)
        input_b.extend(rhs)
        golden.extend(expected)
    if operation == "matvec":
        return GeneratedCase(
            input_a, input_b, golden,
            MLDSA_K * MLDSA_L * N, MLDSA_L * N, MLDSA_K * N,
        )
    return GeneratedCase(input_a, input_b, golden, N, N, N)


def generate_keccak(
    operation: str, batch: int, rng: random.Random
) -> GeneratedCase:
    input_bytes = 32
    output_bytes = 64 if operation != "sha3-256" else 32
    input_a: list[int] = []
    golden: list[int] = []
    for _ in range(batch):
        message = bytes(rng.randrange(256) for _ in range(input_bytes))
        input_a.extend(message)
        if operation == "shake128":
            digest = hashlib.shake_128(message).digest(output_bytes)
        elif operation == "shake256":
            digest = hashlib.shake_256(message).digest(output_bytes)
        elif operation == "sha3-256":
            digest = hashlib.sha3_256(message).digest()
        else:
            digest = hashlib.sha3_512(message).digest()
        golden.extend(digest)
    return GeneratedCase(
        input_a, [0] * (batch * N), golden,
        input_bytes, N, output_bytes,
    )


def cbd(data: bytes, eta: int) -> list[int]:
    result: list[int] = []
    for coefficient in range(N):
        base = coefficient * 2 * eta
        positive = sum(
            (data[(base + bit) >> 3] >> ((base + bit) & 7)) & 1
            for bit in range(eta)
        )
        negative = sum(
            (
                data[(base + eta + bit) >> 3] >>
                ((base + eta + bit) & 7)
            ) & 1
            for bit in range(eta)
        )
        result.append(positive - negative)
    return result


def rejection_sample(data: bytes) -> list[int]:
    result: list[int] = []
    for pos in range(0, len(data) - 2, 3):
        value0 = (data[pos] | (data[pos + 1] << 8)) & 0xFFF
        value1 = ((data[pos + 1] >> 4) | (data[pos + 2] << 4)) & 0xFFF
        if value0 < MLKEM_Q:
            result.append(value0)
        if len(result) < N and value1 < MLKEM_Q:
            result.append(value1)
        if len(result) == N:
            break
    return result + [-1] * (N - len(result))


def generate_sampling(
    operation: str, batch: int, rng: random.Random
) -> GeneratedCase:
    input_bytes = (
        128 if operation == "cbd-eta2"
        else (192 if operation == "cbd-eta3" else 768)
    )
    input_a: list[int] = []
    golden: list[int] = []
    for _ in range(batch):
        data = bytes(rng.randrange(256) for _ in range(input_bytes))
        input_a.extend(data)
        if operation == "cbd-eta2":
            golden.extend(cbd(data, 2))
        elif operation == "cbd-eta3":
            golden.extend(cbd(data, 3))
        else:
            golden.extend(rejection_sample(data))
    return GeneratedCase(input_a, [0] * (batch * N), golden, input_bytes, N, N)


def generate_slhdsa_hash(
    operation: str, batch: int, rng: random.Random
) -> GeneratedCase:
    """Generate SHAKE-192f Hash Jobs using n=24 and full 32-byte ADRS."""
    input_a: list[int] = []
    input_b: list[int] = []
    golden: list[int] = []
    input_size = (
        88 if operation == "wots-chain"
        else (80 if operation == "fors-leaf" else 104)
    )
    for _ in range(batch):
        pk_seed = bytes(rng.randrange(256) for _ in range(24))
        address = bytearray(rng.randrange(256) for _ in range(32))
        node = bytes(rng.randrange(256) for _ in range(24))
        record = list(pk_seed + address + node)
        if operation == "wots-chain":
            start = rng.randrange(16)
            steps = rng.randrange(16 - start)
            current = node
            for step in range(steps):
                address[28:32] = (start + step).to_bytes(4, "big")
                current = hashlib.shake_256(
                    pk_seed + address + current
                ).digest(24)
            record.extend((start, steps))
            record.extend([0] * (input_size - len(record)))
            golden.extend(current)
        elif operation == "fors-leaf":
            golden.extend(
                hashlib.shake_256(
                    pk_seed + address + node
                ).digest(24)
            )
        else:
            right = bytes(rng.randrange(256) for _ in range(24))
            record.extend(right)
            golden.extend(
                hashlib.shake_256(
                    pk_seed + address + node + right
                ).digest(24)
            )
        input_a.extend(record)
        input_b.extend([0] * N)
    return GeneratedCase(input_a, input_b, golden, input_size, N, 24)


def write_int32(path: Path, values: list[int]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(struct.pack(f"<{len(values)}i", *values))


def read_generation_config():
    kernel = os.getenv(
        "PQC_KERNEL", os.getenv("MLKEM_KERNEL", "mlkem-poly")
    )
    operation = os.getenv(
        "PQC_OPERATION", os.getenv("MLKEM_OPERATION", "ntt")
    )
    batch = int(os.getenv(
        "PQC_BATCH", os.getenv("MLKEM_BATCH", "40")
    ))
    seed = int(os.getenv(
        "PQC_SEED", os.getenv("MLKEM_SEED", "910203")
    ))
    if kernel not in KERNEL_CODES:
        raise ValueError(f"PQC_KERNEL must be one of {tuple(KERNEL_CODES)}")
    if operation not in OPERATIONS[kernel]:
        raise ValueError(
            f"PQC_OPERATION for {kernel} must be one of "
            f"{OPERATIONS[kernel]}, got {operation}"
        )
    if batch <= 0:
        raise ValueError("PQC_BATCH must be positive")

    return kernel, operation, batch, seed


def write_ntt_tiling(kernel, batch):
    modulus = MLKEM_Q if kernel == "mlkem-poly" else MLDSA_Q
    generator = 17 if kernel == "mlkem-poly" else 1753
    matrix_prefix = "mlkem" if kernel == "mlkem-poly" else "mldsa"
    forward = (INPUT_DIR / f"M_{matrix_prefix}_forward.bin").read_bytes()
    (INPUT_DIR / "M_phase1.bin").write_bytes(forward)
    (INPUT_DIR / "M_phase2.bin").write_bytes(forward)
    write_int32(INPUT_DIR / "twiddle_phase12.bin", [1])
    q_list = [modulus] + [0] * 15
    g_list = [generator] + [0] * 15
    tiling_values = [
        N, modulus, generator, batch, 16, 16,
        1, N, 0, N, N, 1, 0,
        *q_list, *g_list, 1,
    ]
    if len(tiling_values) != 46:
        raise RuntimeError("invalid NttTilingData field count")
    (INPUT_DIR / "input_tiling.bin").write_bytes(
        struct.pack("<46i", *tiling_values)
    )


def write_generated_case(kernel, operation, batch, generated):
    INPUT_DIR.mkdir(parents=True, exist_ok=True)
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    write_int32(INPUT_DIR / "input_x.bin", generated.input_a)
    write_int32(INPUT_DIR / "input_y.bin", generated.input_b)
    write_int32(OUTPUT_DIR / "golden.bin", generated.golden)
    if kernel == "mlkem-poly" and operation != "basemul":
        write_mlkem_bat_tables()
    elif kernel == "mldsa-poly" and operation != "pointwise":
        write_mldsa_bat_tables()
    if operation == "ntt" and kernel in ("mlkem-poly", "mldsa-poly"):
        write_ntt_tiling(kernel, batch)
    else:
        fields = (batch, KERNEL_CODES[kernel], OPERATIONS[kernel].index(operation),
                  generated.input_a_size, generated.input_b_size,
                  generated.output_size, min(batch, 40))
        (INPUT_DIR / "input_tiling.bin").write_bytes(struct.pack("<7I", *fields))


def main() -> None:
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    kernel, operation, batch, seed = read_generation_config()
    generators = {
        "mlkem-poly": generate_mlkem, "mldsa-poly": generate_mldsa,
        "keccak": generate_keccak, "mlkem-sampling": generate_sampling,
        "slhdsa-hash": generate_slhdsa_hash,
    }
    generate = generators.get(kernel)
    if generate is None:
        raise ValueError(f"Unsupported PQC kernel: {kernel}")
    generated = generate(operation, batch, random.Random(seed))
    write_generated_case(kernel, operation, batch, generated)
    LOGGER.info(
        "[INFO] generated kernel=%s, operation=%s, batch=%d, blockDim=%d, "
        "input_tiling.bin, input_x.bin, input_y.bin, golden.bin",
        kernel, operation, batch, min(batch, 40),
    )


if __name__ == "__main__":
    main()
