#!/usr/bin/env python3
"""Keccak-f[1600] 参考置换，输入和输出均为 uint32[50, B] 布局。"""
from __future__ import annotations

import numbers
import unittest

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



def keccak_f1600_tensor_golden(rows):
    """保持输入不变，返回 24 轮置换后的二维整数列表。"""
    rows = [list(row) for row in rows]
    if len(rows) != 50 or not rows[0] or len(rows[0]) > 8192:
        raise ValueError("expected shape [50, B], 1 <= B <= 8192")
    batch = len(rows[0])
    if any(len(row) != batch for row in rows):
        raise ValueError("ragged input")
    if any(not isinstance(value, numbers.Integral) or value < 0 or value > 0xffffffff
           for row in rows for value in row):
        raise ValueError("expected unsigned 32-bit words")
    output = [[0] * batch for _ in range(50)]
    for message in range(batch):
        words = [int(row[message]) for row in rows]
        keccak_f1600_u32(words)
        for word in range(50):
            output[word][message] = words[word]
    return output


class GoldenTest(unittest.TestCase):
    def test_zero_state(self):
        rows = [[0] * 2 for _ in range(50)]
        actual = keccak_f1600_tensor_golden(rows)
        expected = [0xf1258f7940e1dde7, 0x84d5ccf933c0478a,
                    0xd598261ea65aa9ee, 0xbd1547306f80494d]
        for lane, value in enumerate(expected):
            for message in range(2):
                self.assertEqual(actual[2 * lane][message], value & 0xffffffff)
                self.assertEqual(actual[2 * lane + 1][message], value >> 32)
        self.assertTrue(all(value == 0 for row in rows for value in row))

    def test_invalid_shape(self):
        for rows in ([], [[0]], [[] for _ in range(50)], [[-1] for _ in range(50)]):
            with self.assertRaises(ValueError):
                keccak_f1600_tensor_golden(rows)


if __name__ == "__main__":
    unittest.main()
