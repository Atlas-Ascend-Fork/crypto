#!/usr/bin/env python3
"""将编译后的纯 C 参考实现与 hashlib 逐字节比较。"""

import ctypes
import hashlib
import random
import sys


def main(library):
    function = ctypes.CDLL(library).CannCryptoRefShake
    function.argtypes = [ctypes.c_uint32, ctypes.c_void_p, ctypes.c_size_t,
                         ctypes.c_void_p, ctypes.c_size_t]
    function.restype = ctypes.c_int
    generator = random.Random(20260909)
    count = 0
    for algorithm, rate, oracle in ((128, 168, hashlib.shake_128), (256, 136, hashlib.shake_256)):
        for length in (0, 1, 3, 4, 63, 64, rate - 1, rate, rate + 1,
                       2 * rate - 1, 2 * rate, 2 * rate + 1, 1024, 4096):
            data = bytes(generator.randrange(256) for _ in range(length))
            source = ctypes.create_string_buffer(data)
            for output_length in (1, 3, 31, 32, 64, rate - 1, rate, rate + 1, 2 * rate + 3):
                output = ctypes.create_string_buffer(output_length)
                status = function(algorithm, source, length, output, output_length)
                if status != 0 or output.raw != oracle(data).digest(output_length):
                    raise AssertionError("reference mismatch: {} {} {}".format(algorithm, length, output_length))
                count += 1
    print("REFERENCE_HASHLIB=PASS cases={}".format(count))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_reference_hashlib.py reference_library")
    main(sys.argv[1])
