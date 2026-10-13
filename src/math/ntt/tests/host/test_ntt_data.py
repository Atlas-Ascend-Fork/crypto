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

"""Regression checks for NTT generator refactoring and invalid comparison files."""

import hashlib
import importlib.util
import json
import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest

DATA_DIR = Path(__file__).resolve().parents[1] / "ut/op_kernel/ntt_data"


def load_module(name, filename):
    spec = importlib.util.spec_from_file_location(name, DATA_DIR / filename)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


GENERATOR = load_module("ntt_generator_test", "gen_pqc_data.py")
COMPARE = load_module("ntt_compare_test", "compare_pqc_data.py")
FIXTURES = json.loads(Path(__file__).with_name("ntt_data_golden.json").read_text())


class NttDataTest(unittest.TestCase):
    def test_pqc_generator_regression(self):
        for case in FIXTURES["cases"]:
            with self.subTest(operation=case["operation"], generator=case["generator"]):
                result = getattr(GENERATOR, case["generator"])(
                    case["operation"], case["batch"], random.Random(case["seed"]))
                digest = hashlib.sha256(json.dumps(result, separators=(",", ":")).encode()).hexdigest()
                self.assertEqual(digest, case["sha256"])

    def test_unknown_polynomial_operation(self):
        for generate in (GENERATOR.generate_mlkem, GENERATOR.generate_mldsa):
            with self.subTest(generator=generate.__name__):
                with self.assertRaises(ValueError):
                    generate("unknown", 1, random.Random(910203))

    def test_mlkem_matrix_regression(self):
        matrix = GENERATOR.build_transform_matrix(GENERATOR.mlkem_forward_ntt)
        packed = GENERATOR.build_mlkem_parity_bat_dense_pack(matrix, 3329)
        self.assertEqual(hashlib.sha256(packed).hexdigest(), FIXTURES["mlkem_matrix_sha256"])

    def test_modular_comparison(self):
        self.assertEqual(COMPARE.find_mismatches([8380418], [1], 8380417), [])
        self.assertEqual(COMPARE.find_mismatches([1, 2], [1, 3], None), [(1, 3, 2)])

    def test_comparison_rejects_invalid_files(self):
        with tempfile.TemporaryDirectory() as work:
            output, golden = Path(work) / "output.bin", Path(work) / "golden.bin"
            for payload in (b"", b"x"):
                with self.subTest(payload=payload):
                    output.write_bytes(payload)
                    golden.write_bytes(payload)
                    result = subprocess.run(
                        [sys.executable, str(DATA_DIR / "compare_pqc_data.py"), str(output), str(golden)],
                        env={**os.environ, "PQC_KERNEL": "mlkem-poly", "PQC_OPERATION": "ntt"},
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
                    self.assertNotEqual(result.returncode, 0)

    def test_regular_comparison_rejects_trailing_partial_elements(self):
        import itertools
        import struct
        compare = load_module("ntt_regular_compare_test", "compare_data.py")
        with tempfile.TemporaryDirectory() as work:
            output, golden = Path(work) / "output.bin", Path(work) / "golden.bin"
            aligned = struct.pack("<i", 17)
            for invalid_side, trailing in itertools.product((output, golden), (b"x", b"xy", b"xyz")):
                with self.subTest(side=invalid_side.name, trailing=trailing):
                    output.write_bytes(aligned)
                    golden.write_bytes(aligned)
                    invalid_side.write_bytes(aligned + trailing)
                    self.assertFalse(compare.compare_result(output, golden))
            output.write_bytes(aligned)
            golden.write_bytes(aligned)
            self.assertTrue(compare.compare_result(output, golden))



if __name__ == "__main__":
    unittest.main()
