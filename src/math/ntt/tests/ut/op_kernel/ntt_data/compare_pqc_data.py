#!/usr/bin/env python3
"""Compare an Ascend PQC core-operator result with its generated golden."""

import os
import struct
import sys
from pathlib import Path


def read_int32(path: Path) -> tuple[int, ...]:
    data = path.read_bytes()
    if len(data) % 4 != 0:
        raise ValueError(f"{path} is not int32-aligned: {len(data)} bytes")
    return struct.unpack(f"<{len(data) // 4}i", data)


def main() -> None:
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
    if len(output) != len(golden):
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
    mismatches = [
        (index, expected, actual)
        for index, (actual, expected) in enumerate(zip(output, golden))
        if (
            actual != expected
            if compare_modulus is None
            else (actual - expected) % compare_modulus != 0
        )
    ]
    for index, expected, actual in mismatches[:16]:
        print(
            f"[DIFF] index={index} expected={expected} actual={actual}"
        )
    if mismatches:
        raise SystemExit(
            f"[FAIL] {len(mismatches)}/{len(golden)} coefficients differ"
        )
    relation = (
        f" modulo {compare_modulus}"
        if compare_modulus is not None else ""
    )
    print(
        f"[PASS] all {len(golden)} {kernel}/{operation} values match"
        f"{relation}"
    )


if __name__ == "__main__":
    main()
