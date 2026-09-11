#!/usr/bin/env python3
"""SHAKE128 / SHAKE256 批量字节消息参考结果。"""

import hashlib
import unittest


def shake_golden(messages, output_length, algorithm=128):
    """输入为等长字节串序列，输出为等长字节串列表。"""
    if algorithm not in (128, 256):
        raise ValueError("algorithm must be 128 or 256")
    if not isinstance(output_length, int) or output_length <= 0:
        raise ValueError("output_length must be positive")
    rows = [bytes(message) for message in messages]
    if not rows or any(len(row) != len(rows[0]) for row in rows):
        raise ValueError("messages must have a nonzero batch and uniform length")
    reference = hashlib.shake_128 if algorithm == 128 else hashlib.shake_256
    return [reference(row).digest(output_length) for row in rows]


def shake128_tensor_golden(messages, output_length):
    return shake_golden(messages, output_length, 128)


def shake256_tensor_golden(messages, output_length):
    return shake_golden(messages, output_length, 256)


class GoldenTest(unittest.TestCase):
    def test_empty_message(self):
        self.assertEqual(shake128_tensor_golden([b""], 16)[0].hex(),
                         "7f9c2ba4e88f827d616045507605853e")
        self.assertEqual(shake256_tensor_golden([b""], 16)[0].hex(),
                         "46b9dd2b0ba88d13233b3feb743eeb24")

    def test_batch(self):
        for algorithm, rate in ((128, 168), (256, 136)):
            rows = [bytes([index]) * (rate + 1) for index in range(3)]
            actual = shake_golden(rows, 2 * rate + 3, algorithm)
            self.assertEqual(len(actual), 3)
            self.assertTrue(all(len(row) == 2 * rate + 3 for row in actual))
            self.assertEqual(len(set(actual)), 3)

    def test_invalid_arguments(self):
        for rows, length, algorithm in (([], 1, 128), ([b"", b"a"], 1, 128),
                                        ([b""], 0, 128), ([b""], 1, 512)):
            with self.assertRaises(ValueError):
                shake_golden(rows, length, algorithm)


if __name__ == "__main__":
    unittest.main()
