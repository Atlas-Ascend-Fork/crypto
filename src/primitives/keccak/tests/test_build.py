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

"""Direct-kernel build configuration and CPU debug dependency regression tests."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

MODULE = Path(__file__).resolve().parents[1]


class BuildTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="keccak-test-")
        self.root = Path(self.directory.name)

    def tearDown(self):
        self.directory.cleanup()

    def run_cmake(self, *args):
        cmake = os.environ.get("CANN_TEST_CMAKE") or shutil.which("cmake")
        if not cmake:
            self.skipTest("CMake is required for build configuration tests")
        return subprocess.run(
            [cmake, *map(str, args)], capture_output=True, text=True, timeout=60,
        )

    def make_kernel_test_project(self, linkable=True):
        project = self.root / "kernel-test"
        headers = project / "include/gtest"
        headers.mkdir(parents=True, exist_ok=True)
        declaration = "inline" if linkable else ""
        body = ' { return "ABI"; }' if linkable else ";"
        (headers / "gtest.h").write_text(
            '#include <string>\n'
            '#if _GLIBCXX_USE_CXX11_ABI != 0\n#error Expected ABI=0\n#endif\n'
            'namespace testing { class Message { public:\n'
            'template<class T> Message& operator<<(const T&) { return *this; }\n'
            f'{declaration} std::string GetString() const{body}\n'
            '}; }\n#define EXPECT_EQ(left, right) ((void)((left) == (right)))\n',
            encoding="utf-8",
        )
        (project / "GTestConfig.cmake").write_text(
            'if(NOT TARGET GTest::GTest)\nadd_library(GTest::GTest INTERFACE IMPORTED)\n'
            'add_library(GTest::Main INTERFACE IMPORTED)\n'
            'set_target_properties(GTest::GTest PROPERTIES INTERFACE_INCLUDE_DIRECTORIES '
            f'"{(project / "include").as_posix()}")\n'
            'endif()\nset(GTest_FOUND TRUE)\n', encoding="utf-8",
        )
        (project / "tikicpulibConfig.cmake").write_text(
            'if(NOT TARGET tikicpulib::Ascend910B1)\n'
            'add_library(tikicpulib::Ascend910B1 INTERFACE IMPORTED)\nendif()\n', encoding="utf-8",
        )
        (project / "main.cpp").write_text('int main() { return 0; }\n', encoding="utf-8")
        # Only exercise CMake configuration; the fixture does not run a CANN Kernel.
        (project / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.16)\nproject(kernel_test LANGUAGES CXX)\n'
            'enable_testing()\nset(CMAKE_SYSTEM_NAME Linux)\n'
            'set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)\n'
            f'include("{(MODULE / "cmake/CannOperator.cmake").as_posix()}")\n'
            'add_library(cann_crypto_keccak_reference INTERFACE)\n'
            'cann_kernel_unit_test(first main.cpp)\ncann_kernel_unit_test(second main.cpp)\n'
            'get_test_property(first TIMEOUT first_timeout)\n'
            'get_test_property(second TIMEOUT second_timeout)\n'
            'file(WRITE "${CMAKE_BINARY_DIR}/timeouts.txt" "${first_timeout};${second_timeout}")\n',
            encoding="utf-8",
        )
        return project

    def configure_kernel_test(self, timeout="1800", linkable=True):
        project = self.make_kernel_test_project(linkable)
        args = [
            "-S", project, "-B", self.root / "kernel-binary",
            f"-DPython3_EXECUTABLE={sys.executable}",
            f"-DGTest_DIR={project.as_posix()}", f"-Dtikicpulib_DIR={project.as_posix()}",
            "-DCANN_BUILD_CPU_DEBUG_TESTS=ON", f"-DCANN_KERNEL_UT_TIMEOUT={timeout}",
        ]
        generator = os.environ.get("CANN_TEST_GENERATOR")
        if generator:
            args += ["-G", generator]
        for key in ("CXX_COMPILER", "MAKE_PROGRAM"):
            value = os.environ.get(f"CANN_TEST_{key}")
            if value:
                args.append(f"-DCMAKE_{key}={value}")
        return self.run_cmake(*args)

    def test_kernel_timeout_and_single_abi_check(self):
        result = self.configure_kernel_test()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(
            (self.root / "kernel-binary/timeouts.txt").read_text(encoding="utf-8"), "1800;1800")
        self.assertEqual(result.stdout.count("Performing Test CANN_GTEST_ABI_COMPATIBLE - Success"), 1)

    def test_kernel_timeout_validation(self):
        result = self.configure_kernel_test(timeout="2400")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(
            (self.root / "kernel-binary/timeouts.txt").read_text(encoding="utf-8"), "2400;2400")
        for timeout in ("0", "-1", "invalid"):
            with self.subTest(timeout=timeout):
                result = self.configure_kernel_test(timeout=timeout)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("CANN_KERNEL_UT_TIMEOUT must be a positive integer", result.stderr)

    def test_gtest_probe_requires_linking(self):
        result = self.configure_kernel_test(linkable=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("GTest cannot be compiled and linked", result.stderr)
        self.assertIn("GTest_DIR", result.stderr)

    def test_gtest_probe_rechecks_dependency(self):
        result = self.configure_kernel_test()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = self.configure_kernel_test(linkable=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("GTest cannot be compiled and linked", result.stderr)

    def test_host_only_configuration(self):
        sources = (MODULE, MODULE.parents[1] / "algorithms/hash/shake")
        for source in sources:
            with self.subTest(source=source.name):
                result = self.configure_host(source)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertNotIn("ASC compiler", result.stdout)
                self.assertNotIn("GTest", result.stdout)

    def configure_host(self, source):
        args = [
            "-S", source, "-B", self.root / source.name,
            "-DCANN_BUILD_DEVICE=OFF", "-DCANN_BUILD_TESTS=OFF",
            "-DCANN_BUILD_CPU_DEBUG_TESTS=OFF", f"-DPython3_EXECUTABLE={sys.executable}",
        ]
        generator = os.environ.get("CANN_TEST_GENERATOR")
        if generator:
            args += ["-G", generator]
        for key in ("C_COMPILER", "CXX_COMPILER", "MAKE_PROGRAM"):
            value = os.environ.get(f"CANN_TEST_{key}")
            if value:
                args.append(f"-DCMAKE_{key}={value}")
        return self.run_cmake(*args)


if __name__ == "__main__":
    unittest.main()
