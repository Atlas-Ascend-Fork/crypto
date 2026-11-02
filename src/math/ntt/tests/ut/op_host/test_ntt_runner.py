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

"""SDK-independent regression checks for the kernel-launch command line."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

NTT_DIR = Path(__file__).resolve().parents[3]


class NttRunnerTest(unittest.TestCase):
    def setUp(self):
        self.work = tempfile.TemporaryDirectory(prefix="ntt-runner-")
        self.addCleanup(self.work.cleanup)
        self.sdk = Path(self.work.name) / "sdk with spaces"
        files = {
            "include/acl/acl.h": "",
            "bin/setenv.bash": ":\n",
            "lib64/cmake/asc-config.cmake": "",
            "tools/bisheng_compiler/bin/bisheng": "#!/bin/sh\nprintf 'test compiler\\n'\n",
        }
        for relative, content in files.items():
            path = self.sdk / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8")
        (self.sdk / "tools/bisheng_compiler/bin/bisheng").chmod(0o755)
        self.env = os.environ.copy()
        for key in (
            "ASCEND_CANN_PACKAGE_PATH", "ASCEND_INSTALL_PATH", "ASCEND_HOME_PATH",
            "NTT_PROFILE", "NTT_PYTHON", "NTT_BUILD_JOBS", "ACCEPT_FORCE_REBUILD", "ASCEND_COMPUTE_UNIT",
        ):
            self.env.pop(key, None)
        self.env["NTT_PYTHON"] = sys.executable

    def run_script(self, script, *args, env=None):
        return subprocess.run(
            [str(NTT_DIR / script), *args],
            cwd=self.work.name, env=env or self.env, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
        )

    def test_help_needs_no_sdk(self):
        result = self.run_script("run.sh", "--help")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Default mode: npu", result.stdout)

    def test_missing_values_fail_before_build(self):
        for option in ("-r", "-v", "-i", "-b", "-j", "--stage", "--profile"):
            with self.subTest(option=option):
                result = self.run_script("run.sh", option)
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertIn("Missing value", result.stderr)

    def test_invalid_configuration_fails_before_sdk_lookup(self):
        for args in (
            ("-r", "cpu"), ("-r", "invalid"), ("-v", "invalid"),
            ("--profile", "unknown"), ("--stage", "unknown"),
            ("-j", "0"), ("-b", "unknown"),
        ):
            with self.subTest(args=args):
                result = self.run_script("run.sh", *args)
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertNotIn("Set ASCEND_HOME_PATH", result.stderr)

    def test_default_is_npu_and_build_stays_in_component(self):
        result = self.run_script("run.sh", "--stage", "env", "-i", str(self.sdk))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("[MODE] npu", result.stdout)
        self.assertIn("[COMPUTE UNIT] ascend910b", result.stdout)
        self.assertIn(f"[SDK] {self.sdk}", result.stdout)
        build = next(line[8:] for line in result.stdout.splitlines() if line.startswith("[BUILD] "))
        self.assertTrue(Path(build).is_relative_to(NTT_DIR / "build/asc"))
        self.assertFalse(Path(build).exists(), "env must not create a build directory")

    def test_a2_and_a3_families_work_without_a_device(self):
        for family in ("ascend910b", "ascend910_93"):
            with self.subTest(family=family):
                result = self.run_script("run.sh", "--stage", "env", "-v", family, "-i", str(self.sdk))
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn(f"[COMPUTE UNIT] {family}", result.stdout)
                self.assertIn("[ARCH] dav-2201", result.stdout)

    def test_target_environment_and_explicit_override(self):
        env = {**self.env, "ASCEND_COMPUTE_UNIT": "ascend910_93"}
        inherited = self.run_script("run.sh", "--stage", "env", "-i", str(self.sdk), env=env)
        explicit = self.run_script("run.sh", "--stage", "env", "-i", str(self.sdk), "-v", "ascend910b", env=env)
        self.assertEqual(inherited.returncode, 0, inherited.stderr)
        self.assertEqual(explicit.returncode, 0, explicit.stderr)
        self.assertIn("[COMPUTE UNIT] ascend910_93", inherited.stdout)
        self.assertIn("[COMPUTE UNIT] ascend910b", explicit.stdout)
        inherited_build = next(line for line in inherited.stdout.splitlines() if line.startswith("[BUILD]"))
        explicit_build = next(line for line in explicit.stdout.splitlines() if line.startswith("[BUILD]"))
        self.assertNotEqual(inherited_build, explicit_build)

    def test_legacy_models_normalize_to_families(self):
        for model, family in (("Ascend910B3", "ascend910b"), ("Ascend910_9382", "ascend910_93")):
            with self.subTest(model=model):
                old = self.run_script("run.sh", "--stage", "env", "-i", str(self.sdk), "-v", model)
                new = self.run_script("run.sh", "--stage", "env", "-i", str(self.sdk), "-v", family)
                self.assertEqual(old.returncode, 0, old.stderr)
                self.assertEqual(new.returncode, 0, new.stderr)
                self.assertEqual(old.stdout, new.stdout)

    def test_a3_cannot_silently_use_the_a2_simulator(self):
        result = self.run_script("run.sh", "--stage", "env", "-r", "sim", "-v", "ascend910_93")
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn("Camodel", result.stderr)

    def test_explicit_sdk_overrides_environment(self):
        env = {**self.env, "ASCEND_CANN_PACKAGE_PATH": "/missing-sdk"}
        result = self.run_script("run.sh", "--stage", "env", "-i", str(self.sdk), env=env)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(f"[SDK] {self.sdk}", result.stdout)

    def test_build_type_has_a_separate_cache(self):
        outputs = []
        for build_type in ("Debug", "Release"):
            result = self.run_script(
                "run.sh", "--stage", "env", "-i", str(self.sdk), "-b", build_type)
            self.assertEqual(result.returncode, 0, result.stderr)
            outputs.append(next(line for line in result.stdout.splitlines() if line.startswith("[BUILD] ")))
        self.assertNotEqual(*outputs)

    def test_legacy_hook_forwards_to_canonical_runner(self):
        env = {**self.env, "ASCEND_CANN_PACKAGE_PATH": str(self.sdk)}
        direct = self.run_script(
            "run.sh", "--stage", "env", "-r", "npu", "-v", "Ascend910_9382", "-j", "2", env=env)
        legacy = self.run_script(
            "scripts/run_npu_server.sh", "env", "--mode", "npu", "--soc", "Ascend910_9382", "--jobs", "2",
            env=env)
        self.assertEqual(direct.returncode, 0, direct.stderr)
        self.assertEqual(legacy.returncode, 0, legacy.stderr)
        self.assertEqual(direct.stdout, legacy.stdout)


if __name__ == "__main__":
    unittest.main()
