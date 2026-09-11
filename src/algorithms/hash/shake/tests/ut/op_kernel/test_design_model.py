#!/usr/bin/env python3

from __future__ import annotations

import hashlib
import random


MASK64 = (1 << 64) - 1
ROUND_CONSTANTS = [
    0x0000000000000001, 0x0000000000008082,
    0x800000000000808A, 0x8000000080008000,
    0x000000000000808B, 0x0000000080000001,
    0x8000000080008081, 0x8000000000008009,
    0x000000000000008A, 0x0000000000000088,
    0x0000000080008009, 0x000000008000000A,
    0x000000008000808B, 0x800000000000008B,
    0x8000000000008089, 0x8000000000008003,
    0x8000000000008002, 0x8000000000000080,
    0x000000000000800A, 0x800000008000000A,
    0x8000000080008081, 0x8000000000008080,
    0x0000000080000001, 0x8000000080008008,
]
RHO = [
    0, 1, 62, 28, 27,
    36, 44, 6, 55, 20,
    3, 10, 43, 25, 39,
    41, 45, 15, 21, 8,
    18, 2, 61, 56, 14,
]


def rotl64(value: int, shift: int) -> int:
    if shift == 0:
        return value
    return ((value << shift) | (value >> (64 - shift))) & MASK64


def keccak_f1600_u32(words: list[int]) -> None:
    state = [words[2 * lane] | (words[2 * lane + 1] << 32)
             for lane in range(25)]

    for round_constant in ROUND_CONSTANTS:
        column = [
            state[x] ^ state[x + 5] ^ state[x + 10] ^
            state[x + 15] ^ state[x + 20]
            for x in range(5)
        ]
        delta = [
            column[(x + 4) % 5] ^ rotl64(column[(x + 1) % 5], 1)
            for x in range(5)
        ]
        for y in range(5):
            for x in range(5):
                state[x + 5 * y] ^= delta[x]

        permuted = [0] * 25
        for y in range(5):
            for x in range(5):
                source = x + 5 * y
                destination = y + 5 * ((2 * x + 3 * y) % 5)
                permuted[destination] = rotl64(state[source], RHO[source])

        for y in range(5):
            for x in range(5):
                state[x + 5 * y] = (
                    permuted[x + 5 * y]
                    ^ ((~permuted[(x + 1) % 5 + 5 * y])
                       & permuted[(x + 2) % 5 + 5 * y])
                ) & MASK64
        state[0] ^= round_constant

    for lane, value in enumerate(state):
        words[2 * lane] = value & 0xFFFFFFFF
        words[2 * lane + 1] = value >> 32


def xor_bytes(words: list[int], block: bytes) -> None:
    for position, byte in enumerate(block):
        words[position // 4] ^= byte << (8 * (position % 4))


def squeeze_bytes(words: list[int], count: int) -> bytes:
    return bytes(
        (words[position // 4] >> (8 * (position % 4))) & 0xFF
        for position in range(count)
    )


def tensor_shake(message: bytes, output_length: int, rate: int) -> bytes:
    words = [0] * 50
    offset = 0
    while len(message) - offset >= rate:
        xor_bytes(words, message[offset:offset + rate])
        keccak_f1600_u32(words)
        offset += rate

    remainder = message[offset:]
    xor_bytes(words, remainder)
    words[len(remainder) // 4] ^= 0x1F << (8 * (len(remainder) % 4))
    words[(rate - 1) // 4] ^= 0x80 << (8 * ((rate - 1) % 4))
    keccak_f1600_u32(words)

    output = bytearray()
    while len(output) < output_length:
        chunk = min(rate, output_length - len(output))
        output.extend(squeeze_bytes(words, chunk))
        if len(output) < output_length:
            keccak_f1600_u32(words)
    return bytes(output)


def run_algorithm(name: str, rate: int, reference) -> None:
    lengths = sorted({
        0, 1, 3, 4, 63, 64,
        rate - 1, rate, rate + 1,
        2 * rate - 1, 2 * rate, 2 * rate + 1,
        1024,
    })
    outputs = sorted({
        1, 3, 31, 32, 64,
        rate - 1, rate, rate + 1,
        2 * rate + 3,
    })
    generator = random.Random(20260803 + rate)

    tested = 0
    for input_length in lengths:
        message = bytes(generator.randrange(256) for _ in range(input_length))
        for output_length in outputs:
            actual = tensor_shake(message, output_length, rate)
            expected = reference(message).digest(output_length)
            if actual != expected:
                raise AssertionError(
                    f"{name} mismatch input={input_length} "
                    f"output={output_length}")
            tested += 1
    print(f"[通过] {name} 设计模型用例数={tested}")


def main() -> None:
    run_algorithm("SHAKE128", 168, hashlib.shake_128)
    run_algorithm("SHAKE256", 136, hashlib.shake_256)
    print("DESIGN_MODEL_RESULT=PASS")


if __name__ == "__main__":
    main()
