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

import logging
from pathlib import Path
import sys

import numpy as np

LOGGER = logging.getLogger(__name__)
RELATIVE_TOL = 0
ABSOLUTE_TOL = 0
ERROR_TOL = 0


def log_mismatches(output, golden, indexes):
    for index in indexes[:101]:
        expected, actual = int(golden[index]), int(output[index])
        LOGGER.error("data index: %06d, expected: %d, actual: %d, diff: %d",
                     index, expected, actual, actual - expected)


def compare_result(output, golden):
    element_size = np.dtype(np.int32).itemsize
    for path in (output, golden):
        byte_count = Path(path).stat().st_size
        if byte_count % element_size:
            LOGGER.error("invalid int32 byte count: %s has %d bytes", path, byte_count)
            return False
    actual = np.fromfile(output, dtype=np.int32).reshape(-1)
    expected = np.fromfile(golden, dtype=np.int32).reshape(-1)
    if actual.size == 0 or actual.size != expected.size:
        LOGGER.error("invalid sizes: output=%d, golden=%d", actual.size, expected.size)
        return False
    matches = np.isclose(actual, expected, rtol=RELATIVE_TOL,
                         atol=ABSOLUTE_TOL, equal_nan=True)
    indexes = np.flatnonzero(~matches)
    error_ratio = float(indexes.size) / expected.size
    if indexes.size:
        log_mismatches(actual, expected, indexes)
    LOGGER.info("error ratio: %.4f, tolerance: %.4f", error_ratio, ERROR_TOL)
    return error_ratio <= ERROR_TOL


def main():
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    if len(sys.argv) != 3:
        LOGGER.error("Usage: %s OUTPUT GOLDEN", sys.argv[0])
        return 1
    try:
        if not compare_result(sys.argv[1], sys.argv[2]):
            return 1
    except (OSError, ValueError) as error:
        LOGGER.error("%s", error)
        return 1
    LOGGER.info("test pass")
    return 0


if __name__ == '__main__':
    sys.exit(main())
