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

"""Compare an Ascend PQC core-operator result with its generated golden."""

import logging
import os
import struct
import sys
from pathlib import Path


LOGGER = logging.getLogger(__name__)


def read_int32(path: Path) -> tuple[int, ...]:
    data = path.read_bytes()
    if len(data) % 4 != 0:
        raise ValueError(f"{path} is not int32-aligned: {len(data)} bytes")
    return struct.unpack(f"<{len(data) // 4}i", data)


def find_mismatches(output, golden, modulus):
    mismatches = []
    for index, (actual, expected) in enumerate(zip(output, golden)):
        difference = actual - expected
        if modulus is not None:
            difference %= modulus
        if difference:
            mismatches.append((index, expected, actual))
    return mismatches


def main() -> None:
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    if len(sys.argv) != 3:
        raise SystemExit(f"Usage: {sys.argv[0]} OUTPUT GOLDEN")
    kernel = os.getenv(
        "PQC_KERNEL", os.getenv("MLKEM_KERNEL", "mlkem-poly")
    )
    operation = os.getenv(
        "PQC_OPERATION", os.getenv("MLKEM_OPERATION", "ntt")
    )
    output = read_int32(Path(sys.argv[1]))
    golden = read_int32(Path(sys.argv[2]))
    if not output or len(output) != len(golden):
        raise SystemExit(
            f"[FAIL] element count differs: output={len(output)}, "
            f"golden={len(golden)}"
        )

    compare_modulus = (
        8380417
        if kernel == "mldsa-poly"
        and operation in ("ntt", "intt", "roundtrip", "matvec")
        else None
    )
    mismatches = find_mismatches(output, golden, compare_modulus)
    for index, expected, actual in mismatches[:16]:
        LOGGER.error("[DIFF] index=%d expected=%d actual=%d", index, expected, actual)
    if mismatches:
        raise SystemExit(
            f"[FAIL] {len(mismatches)}/{len(golden)} coefficients differ"
        )
    relation = (
        f" modulo {compare_modulus}"
        if compare_modulus is not None else ""
    )
    LOGGER.info("[PASS] all %d %s/%s values match%s", len(golden), kernel, operation, relation)


if __name__ == "__main__":
    main()
