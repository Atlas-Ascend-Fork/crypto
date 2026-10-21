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
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

DATA_DIR = Path(__file__).resolve().parents[3] / "scripts"


def load_module(name, filename):
    spec = importlib.util.spec_from_file_location(name, DATA_DIR / filename)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


GENERATOR = load_module("ntt_generator_test", "gen_pqc_data.py")
COMPARE = load_module("ntt_compare_test", "compare_pqc_data.py")
FIXTURES = json.loads((Path(__file__).resolve().parents[2] / "assets/ntt_data_golden.json").read_text())


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

    def test_u32_primality_rejects_four_base_pseudoprime(self):
        generator = load_module("ntt_regular_generator_test", "gen_data.py")
        composite = 151 * 751 * 28351
        self.assertEqual(composite, 3215031751)
        self.assertLess(composite, 2**32)
        self.assertFalse(generator.is_prime_u32(composite))
        self.assertTrue(generator.is_prime_u32(4294967291))

    def test_u32_primality_matches_small_trial_division(self):
        import math
        generator = load_module("ntt_regular_generator_small_test", "gen_data.py")
        for number in range(10000):
            expected = number >= 2 and all(number % d for d in range(2, math.isqrt(number) + 1))
            self.assertEqual(generator.is_prime_u32(number), expected, number)




class NttGeneratorBoundsTest(unittest.TestCase):
    def setUp(self):
        self.work = tempfile.TemporaryDirectory(prefix="ntt-g-bounds-")
        self.addCleanup(self.work.cleanup)
        self.root = Path(self.work.name)

    def run_generator(self, values):
        env = {key: value for key, value in os.environ.items()
               if not key.startswith("NTT_")}
        env.update({"NTT_N": "32", "NTT_Q": "97", "NTT_G": "5",
                    "NTT_W": "16", "NTT_BLOCK_DIM": "1",
                    "NTT_BENCH": "1", "NTT_SEED": "99"})
        env.update(values)
        return subprocess.run(
            [sys.executable, str(DATA_DIR / "gen_data.py")],
            cwd=self.root, env=env, capture_output=True, text=True,
            timeout=60, check=False)

    def reject_generator(self, values, channel):
        result = self.run_generator(values)
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(f"g[{channel}]", result.stderr)
        self.assertFalse((self.root / "input").exists())
        self.assertFalse((self.root / "output").exists())

    def accept_generator(self, values, moduli, generators):
        result = self.run_generator(values)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        raw = (self.root / "input/input_tiling.bin").read_bytes()
        fields = struct.unpack("<46i", raw)
        self.assertEqual(fields[11], len(moduli))
        self.assertEqual(list(fields[13:13 + len(moduli)]), moduli)
        self.assertEqual(list(fields[29:29 + len(generators)]), generators)
        self.assertEqual(fields[2], generators[0])
        expected_bytes = 32 * 4 * len(moduli)
        self.assertEqual((self.root / "input/input_x.bin").stat().st_size, expected_bytes)
        self.assertEqual((self.root / "output/golden.bin").stat().st_size, expected_bytes)

    def test_reject_negative_generator(self):
        self.reject_generator({"NTT_G": "-1"}, 0)

    def test_reject_zero_generator(self):
        self.reject_generator({"NTT_G": "0"}, 0)

    def test_reject_one_generator(self):
        self.reject_generator({"NTT_G": "1"}, 0)

    def test_reject_generator_equal_to_modulus(self):
        self.reject_generator({"NTT_G": "97"}, 0)

    def test_reject_generator_above_modulus(self):
        self.reject_generator({"NTT_G": "98"}, 0)

    def test_reject_invalid_second_rns_generator(self):
        self.reject_generator(
            {"NTT_Q_LIST": "97,193", "NTT_G_LIST": "5,1"}, 1)

    def test_accept_single_generator(self):
        self.accept_generator({}, [97], [5])
        golden = struct.unpack("<32i", (self.root / "output/golden.bin").read_bytes())
        self.assertEqual(golden, (
            36, 22, 43, 74, 43, 70, 47, 38, 12, 85, 75, 68, 90, 85, 23, 41,
            52, 96, 91, 18, 35, 69, 55, 57, 51, 89, 94, 90, 4, 19, 16, 92))

    def test_accept_lower_in_range_generator(self):
        self.accept_generator({"NTT_G": "2"}, [97], [2])

    def test_accept_upper_in_range_generator(self):
        self.accept_generator({"NTT_G": "96"}, [97], [96])

    def test_accept_rns_generators(self):
        self.accept_generator(
            {"NTT_Q_LIST": "97,193", "NTT_G_LIST": "5,19"}, [97, 193], [5, 19])


class NttGeneratorLayoutTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory(prefix="ntt-layout-host-")
        cls.addClassCleanup(cls.work.cleanup)
        cls.binary = Path(cls.work.name) / "check_ntt_generator_layout"
        compiler = shutil.which("c++")
        if compiler is None:
            raise unittest.SkipTest("a C++ compiler is required to check the production Host tiling")
        ntt_root = Path(__file__).resolve().parents[3]
        subprocess.run([
            compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
            f"-I{ntt_root}", f"-I{ntt_root / 'op_host'}", str(ntt_root / "op_host/ntt_tiling.cpp"),
            str(Path(__file__).with_name("test_ntt_generator_layout.cpp")),
            "-o", str(cls.binary),
        ], check=True, capture_output=True, text=True)

    def check_generated_case(self, settings, script="gen_data.py"):
        env = {key: value for key, value in os.environ.items() if not key.startswith(("NTT_", "PQC_"))}
        env.update({"NTT_N": "256", "NTT_Q": "3329", "NTT_G": "17",
                    "NTT_BLOCK_DIM": "16", "NTT_W": "16", "NTT_BENCH": "1",
                    "NTT_CHANNEL_BLOCK": "1", "NTT_RNS_CHANNELS": "1", "NTT_SEED": "99"})
        env.update(settings)
        with tempfile.TemporaryDirectory(prefix="ntt-layout-case-") as directory:
            result = subprocess.run(
                [sys.executable, str(DATA_DIR / script)], cwd=directory,
                env=env, capture_output=True, text=True, timeout=60, check=False)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            host = subprocess.run(
                [str(self.binary), directory], capture_output=True, text=True, timeout=15, check=False)
            self.assertEqual(host.returncode, 0, host.stdout + host.stderr)

    def test_generic_ntt_with_pqc_moduli_matches_production_host(self):
        cases = [
            {},
            {"NTT_Q": "8380417", "NTT_G": "1753"},
            {"NTT_BLOCK_DIM": "8"},
            {"NTT_W": "32"},
            {"NTT_CHANNEL_BLOCK": "2"},
            {"NTT_Q_LIST": "3329,8380417", "NTT_G_LIST": "17,1753",
             "NTT_RNS_CHANNELS": "2", "NTT_CHANNEL_BLOCK": "2", "NTT_BLOCK_DIM": "8"},
        ]
        for settings in cases:
            with self.subTest(settings=settings):
                self.check_generated_case(settings)

    def test_other_layouts_match_production_host(self):
        cases = [
            {"NTT_Q": "7681"},
            {"NTT_Q": "7681", "NTT_BLOCK_DIM": "1"},
            {"NTT_N": "128"},
            {"NTT_N": "512", "NTT_Q": "12289", "NTT_G": "11"},
            {"NTT_N": "32"},
            {"NTT_Q_LIST": "7681,3329", "NTT_G_LIST": "17,17",
             "NTT_RNS_CHANNELS": "2", "NTT_CHANNEL_BLOCK": "2", "NTT_BLOCK_DIM": "8"},
        ]
        for settings in cases:
            with self.subTest(settings=settings):
                self.check_generated_case(settings)

    def test_dedicated_pqc_layouts_match_production_host(self):
        for kernel in ("mlkem-poly", "mldsa-poly"):
            with self.subTest(kernel=kernel):
                self.check_generated_case({
                    "PQC_KERNEL": kernel, "PQC_OPERATION": "ntt", "PQC_BATCH": "1",
                    "NTT_DATA_DIR": ".",
                }, script="gen_pqc_data.py")


if __name__ == "__main__":
    unittest.main()
