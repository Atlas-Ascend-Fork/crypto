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


import subprocess
import sys
from pathlib import Path
import tempfile
import unittest

MODULE = Path(__file__).resolve().parents[1]
SCRIPT = MODULE / "cmake/stage.py"


class StagePathTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="crypto-stage-path-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.output = self.root / "generated project"

    def mark_output(self):
        self.output.mkdir()
        (self.output / ".cann-generated").write_text("cann-crypto-sdk\n", encoding="utf-8")

    def stage_cli(self, shake=False):
        command = [sys.executable, str(SCRIPT), "--output", str(self.output), "--module", str(MODULE)]
        if shake:
            command += ["--module", str(MODULE.parents[1] / "algorithms/hash/shake")]
        return subprocess.run(command, capture_output=True, text=True, timeout=60)

    def test_reject_cmake_link_without_overwriting_external_source(self):
        self.mark_output()
        outside = self.root / "outside developer source.cpp"
        original = b"// retain the existing source\n"
        outside.write_bytes(original)
        link = self.output / "CMakeLists.txt"
        link.symlink_to(outside)
        result = self.stage_cli()
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("符号链接", result.stderr)
        self.assertEqual(outside.read_bytes(), original)
        self.assertTrue(link.is_symlink())
        self.assertEqual(sorted(p.name for p in self.output.iterdir()), [".cann-generated", "CMakeLists.txt"])

    def test_reject_dangling_cmake_link_without_creating_external_file(self):
        self.mark_output()
        outside = self.root / "missing outside.cpp"
        link = self.output / "CMakeLists.txt"
        link.symlink_to(outside)
        result = self.stage_cli()
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertFalse(outside.exists())
        self.assertTrue(link.is_symlink())

    def test_reject_linked_ownership_marker_without_external_write(self):
        self.output.mkdir()
        outside = self.root / "external marker"
        outside.write_text("cann-crypto-sdk\n", encoding="utf-8")
        timestamp = outside.stat().st_mtime_ns
        (self.output / ".cann-generated").symlink_to(outside)
        result = self.stage_cli()
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertEqual(outside.stat().st_mtime_ns, timestamp)
        self.assertEqual([p.name for p in self.output.iterdir()], [".cann-generated"])

    def test_existing_source_output_link_guard_remains_active(self):
        self.mark_output()
        host = self.output / "op_host"
        host.mkdir()
        original_source = next((MODULE / "op_host").glob("*.cpp"))
        outside = self.root / "protected source.cpp"
        outside.write_bytes(b"// preserved\n")
        (host / original_source.name).symlink_to(outside)
        result = self.stage_cli()
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertEqual(outside.read_bytes(), b"// preserved\n")

    def check_reconfiguration(self, shake=False):
        result = self.stage_cli(shake)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        expected = (MODULE / "cmake/device/CMakeLists.txt").read_bytes()
        self.assertEqual((self.output / "CMakeLists.txt").read_bytes(), expected)
        snapshots = {p.relative_to(self.output): (p.read_bytes(), p.stat().st_mtime_ns)
                     for p in self.output.rglob("*") if p.is_file() and p.name != ".cann-generated"}
        self.assertTrue((self.output / "op_kernel/keccak_f1600_u32_vector.h").is_file())
        result = self.stage_cli(shake)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for relative, state in snapshots.items():
            item = self.output / relative
            self.assertEqual((item.read_bytes(), item.stat().st_mtime_ns), state)

    def test_normal_keccak_stage_and_unchanged_reconfiguration(self):
        self.check_reconfiguration()

    def test_normal_shake_stage_and_unchanged_reconfiguration(self):
        self.check_reconfiguration(shake=True)


if __name__ == "__main__":
    unittest.main()
