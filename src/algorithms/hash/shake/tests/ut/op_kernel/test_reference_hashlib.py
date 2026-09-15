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

"""将编译后的纯 C 参考实现与 hashlib 逐字节比较。"""

import ctypes
import hashlib
import logging
import random
import sys


def check_output(function, algorithm, source, data, output_length, oracle):
    output = ctypes.create_string_buffer(output_length)
    status = function(algorithm, source, len(data), output, output_length)
    if status != 0 or output.raw != oracle(data).digest(output_length):
        raise AssertionError("reference mismatch: {} {} {}".format(algorithm, len(data), output_length))


def run_algorithm(function, algorithm, rate, oracle, generator):
    count = 0
    for length in (
        0, 1, 3, 4, 63, 64, rate - 1, rate, rate + 1,
        2 * rate - 1, 2 * rate, 2 * rate + 1, 1024, 4096,
    ):
        data = bytes(generator.randrange(256) for _ in range(length))
        source = ctypes.create_string_buffer(data)
        for output_length in (1, 3, 31, 32, 64, rate - 1, rate, rate + 1, 2 * rate + 3):
            check_output(function, algorithm, source, data, output_length, oracle)
            count += 1
    return count


def main(library):
    function = ctypes.CDLL(library).CannCryptoRefShake
    function.argtypes = [
        ctypes.c_uint32, ctypes.c_void_p, ctypes.c_size_t,
        ctypes.c_void_p, ctypes.c_size_t,
    ]
    function.restype = ctypes.c_int
    generator = random.Random(20260909)
    count = 0
    for algorithm, rate, oracle in ((128, 168, hashlib.shake_128), (256, 136, hashlib.shake_256)):
        count += run_algorithm(function, algorithm, rate, oracle, generator)
    logging.info("REFERENCE_HASHLIB=PASS cases=%d", count)


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO, format="%(message)s", stream=sys.stdout)
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_reference_hashlib.py reference_library")
    main(sys.argv[1])
