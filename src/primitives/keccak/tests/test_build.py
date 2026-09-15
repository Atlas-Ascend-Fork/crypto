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

"""标准算子工程生成与包接口检查的单元测试。"""

import importlib.util
import os
import shutil
import subprocess
import sys
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


MODULE = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


stage = load("stage", MODULE / "cmake/stage.py")
package = load("verify_package", MODULE / "cmake/verify_package.py")

class BuildTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="keccak-test-")
        self.root = Path(self.directory.name)

    def tearDown(self):
        self.directory.cleanup()

    def test_stage_and_reconfigure(self):
        output = self.root / "project"
        stage.stage(output, [MODULE])
        source = output / "op_kernel/keccak_f1600_u32_vector.h"
        before = source.stat().st_mtime_ns
        stage.stage(output, [MODULE])
        self.assertEqual(before, source.stat().st_mtime_ns)
        # 生成工程统一使用 LF，允许源码在 Windows 下使用 CRLF。
        self.assertEqual(
            source.read_text(encoding="utf-8"),
            (MODULE / "op_kernel/keccak_f1600_u32_vector.h").read_text(encoding="utf-8"),
        )

    def test_rewrite_includes_per_source(self):
        first = self.root / "first"
        second = self.root / "second"
        first.mkdir()
        second.mkdir()
        owned = {first / "local.h"}
        for directory in (first, second):
            source = directory / "kernel.cpp"
            source.write_text('#include "local.h"\n#include "external.h"\n', encoding="utf-8")
            self.assertEqual(stage.rewrite_includes(source, owned), source.read_text(encoding="utf-8"))
        source = second / "kernel.cpp"
        source.write_text('#include "../first/local.h"\n', encoding="utf-8")
        self.assertEqual(stage.rewrite_includes(source, owned), '#include "local.h"\n')

    def test_unowned_directory(self):
        with self.assertRaises(ValueError):
            stage.stage(self.root, [MODULE])

    def test_reject_source_directory(self):
        with self.assertRaises(ValueError):
            stage.stage(MODULE, [MODULE])

    def test_stale_source(self):
        output = self.root / "project"
        stage.stage(output, [MODULE])
        (output / "op_host/removed_operator.cpp").write_text("// stale\n")
        with self.assertRaises(ValueError):
            stage.stage(output, [MODULE])

    def test_duplicate_sources(self):
        with self.assertRaises(ValueError):
            stage.stage(self.root / "project", [MODULE, MODULE])

    def test_shake_shared_sources(self):
        shake = MODULE.parents[1] / "algorithms/hash/shake"
        output = self.root / "project"
        stage.stage(output, [MODULE, shake])
        self.assertTrue((output / "op_kernel/keccak_f1600_tensor.h").is_file())
        self.assertTrue((output / "op_host/shake_launch.cpp").is_file())
        self.assertTrue((output / "op_host/shake_tiling_common.h").is_file())
        self.assertTrue((output / "op_host/shake_tensor_def.h").is_file())
        common = (output / "op_host/shake_tiling_common.h").read_text(encoding="utf-8")
        self.assertIn('#include "shake_internal.h"', common)
        for bits in (128, 256):
            tiling = (output / f"op_host/shake{bits}_tensor_tiling.cpp").read_text(encoding="utf-8")
            definition = (output / f"op_host/shake{bits}_tensor_def.cpp").read_text(encoding="utf-8")
            self.assertIn('#include "shake_tiling_common.h"', tiling)
            self.assertIn('#include "shake_tensor_def.h"', definition)
        content = (output / "op_kernel/shake_uniform_u32_tensor.h").read_text(encoding="utf-8")
        self.assertIn('#include "keccak_f1600_u32_vector.h"', content)

    def test_stage_uses_installed_framework(self):
        output = self.root / "project"
        stage.stage(output, [MODULE])
        self.assertFalse((output / "cmake").exists())
        self.assertFalse((output / "scripts").exists())
        self.assertEqual(
            (output / "CMakeLists.txt").read_text(encoding="utf-8"),
            (MODULE / "cmake/device/CMakeLists.txt").read_text(encoding="utf-8"),
        )
        before = (output / "CMakeLists.txt").stat().st_mtime_ns
        stage.stage(output, [MODULE])
        self.assertEqual(before, (output / "CMakeLists.txt").stat().st_mtime_ns)

    def test_reject_legacy_project(self):
        output = self.root / "project"
        output.mkdir()
        (output / ".cann-generated").write_text("cann-crypto\n", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "新的构建目录"):
            stage.stage(output, [MODULE])

    def make_framework(self, relative):
        framework = self.root / relative
        shutil.copytree(MODULE / "tests/assets/cann_framework_stub", framework)
        return framework

    def run_cmake(self, *args):
        cmake = os.environ.get("CANN_TEST_CMAKE") or shutil.which("cmake")
        if not cmake:
            self.skipTest("CMake is required for framework configuration tests")
        return subprocess.run(
            [cmake, *map(str, args)], capture_output=True, text=True, timeout=60,
        )

    def find_framework(self, sdk, override=None):
        script = self.root / "find_framework.cmake"
        content = (
            f'set(ASCEND_CANN_PACKAGE_PATH "{sdk.as_posix()}")\n'
            'set(CMAKE_SYSTEM_PROCESSOR aarch64)\n'
            f'include("{(MODULE / "cmake/CannFramework.cmake").as_posix()}")\n'
        )
        if override:
            content += f'set(CANN_CMAKE_DIR "{override.as_posix()}")\n'
        content += (
            'cann_find_framework()\n'
            f'file(WRITE "{(self.root / "selected.txt").as_posix()}" "${{CANN_CMAKE_DIR}}")\n'
        )
        script.write_text(content, encoding="utf-8")
        return self.run_cmake("-P", script)

    def test_find_framework_layouts(self):
        for root in ("", "toolkit", "aarch64-linux"):
            for tool in ("compiler", "tools"):
                with self.subTest(root=root, tool=tool):
                    self.check_framework_layout(root, tool)

    def check_framework_layout(self, root, tool):
        sdk = self.root / f"sdk-{root or 'root'}-{tool}"
        framework = self.make_framework(
            sdk.relative_to(self.root) / root / tool / "tikcpp/ascendc_kernel_cmake/fwk_modules")
        result = self.find_framework(sdk)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.root / "selected.txt").read_text(encoding="utf-8"), framework.as_posix())

    def test_find_framework_override(self):
        framework = self.make_framework("custom/framework")
        result = self.find_framework(self.root / "sdk", framework)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.root / "selected.txt").read_text(encoding="utf-8"), framework.as_posix())

    def test_missing_framework(self):
        result = self.find_framework(self.root / "sdk")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("CANN operator CMake framework is unavailable", result.stderr)
        self.assertIn("CANN_CMAKE_DIR", result.stderr)

    def test_incomplete_framework(self):
        framework = self.root / "sdk/compiler/tikcpp/ascendc_kernel_cmake/fwk_modules"
        framework.mkdir(parents=True)
        (framework / "config.cmake").touch()
        result = self.find_framework(self.root / "sdk")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("intf.cmake", result.stderr)

    def configure_project(self, output, framework, name, soc):
        args = [
            "-S", output, "-B", self.root / "binary",
            f"-DCANN_CMAKE_DIR={framework.as_posix()}", f"-Dvendor_name={name}",
            f"-DASCEND_COMPUTE_UNIT={soc}", f"-DASCEND_PYTHON_EXECUTABLE={sys.executable}",
            f"-DCMAKE_INSTALL_PREFIX={(self.root / 'opp').as_posix()}",
        ]
        generator = os.environ.get("CANN_TEST_GENERATOR")
        if generator:
            args += ["-G", generator]
        for key in ("C_COMPILER", "CXX_COMPILER", "MAKE_PROGRAM"):
            value = os.environ.get(f"CANN_TEST_{key}")
            if value:
                args.append(f"-DCMAKE_{key}={value}")
        return self.run_cmake(*args)

    def check_project_configuration(self, name, soc):
        modules = [MODULE]
        if name == "shake":
            modules.append(MODULE.parents[1] / "algorithms/hash/shake")
        output = self.root / "project"
        stage.stage(output, modules)
        framework = self.make_framework("framework")
        result = self.configure_project(output, framework, name, soc)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        calls = (self.root / "binary/framework_calls.txt").read_text(encoding="utf-8")
        self.assertIn(f"package;{name};TYPE;RUN", calls)
        self.assertIn(f"PYTHON;{sys.executable}", calls)
        code_gen = next(line for line in calls.splitlines() if line.startswith("code_gen;"))
        self.assertIn(f"-I{(output / 'op_host').as_posix()}", code_gen)
        self.assertIn(f"-I{(output / 'op_kernel').as_posix()}", code_gen)
        self.assertIn("library;cust_opapi;ACLNN;", calls)
        self.assertIn("library;cust_optiling;TILING;", calls)
        self.assertIn("fallback_test.cpp", calls)
        self.assertIn("kernel_library;ascendc_kernels;SRC_BASE;", calls)
        self.assertIn("TILING_LIBRARY;cust_optiling", calls)
        self.assertIn(f"package_add;{name};LIBRARY;cust_opapi;cust_optiling;cust_op_proto;ascendc_kernels", calls)
        self.assertNotIn("sha3_ref.c", calls)
        self.assertNotIn("OpenSSL", calls)
        expected = ["KeccakF1600Tensor"]
        if name == "shake":
            expected += ["Shake128Tensor", "Shake256Tensor"]
        mapping = self.root / "binary/autogen/ascendc_kernels_custom_source_files.ini"
        self.assertEqual(mapping.read_text(encoding="utf-8").splitlines(), expected)
        self.assertEqual(calls.count("kernel_source;"), len(expected))
        headers = list((self.root / "binary/autogen").glob("aclnn_*.h"))
        self.assertEqual(len(headers), len(expected))
        result = self.configure_project(output, framework, name, soc)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(mapping.read_text(encoding="utf-8").splitlines(), expected)

    def test_keccak_project_configuration(self):
        self.check_project_configuration("keccak", "ascend910b")

    def test_shake_project_configuration(self):
        self.check_project_configuration("shake", "ascend910_93")

    def test_codegen_requires_kernel_include_path(self):
        output = self.root / "project"
        stage.stage(output, [MODULE])
        template = output / "CMakeLists.txt"
        content = template.read_text(encoding="utf-8")
        template.write_text(
            content.replace('"-I${CMAKE_CURRENT_SOURCE_DIR}/op_kernel"', ""), encoding="utf-8")
        framework = self.make_framework("framework")
        result = self.configure_project(output, framework, "keccak", "ascend910b")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Code generation header probe failed", result.stderr)
        self.assertIn("keccak_f1600_tensor_tiling_data.h", result.stderr)

    def test_missing_framework_api(self):
        output = self.root / "project"
        stage.stage(output, [MODULE])
        framework = self.make_framework("framework")
        (framework / "func.cmake").write_text("", encoding="utf-8")
        result = self.configure_project(output, framework, "keccak", "ascend910b")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("does not provide npu_op_package", result.stderr)

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

    def test_package_missing_header(self):
        with self.assertRaises(ValueError):
            package.verify(self.root, "keccak")

    def test_package_symbols(self):
        headers = self.root / "op_api/include"
        headers.mkdir(parents=True)
        (headers / "aclnn_keccak_f1600_tensor.h").touch()
        symbols = "0 T aclnnKeccakF1600Tensor\n0 T aclnnKeccakF1600TensorGetWorkspaceSize\n"
        with patch.object(package.subprocess, "check_output", return_value=symbols):
            package.verify(self.root, "keccak")
        with patch.object(package.subprocess, "check_output", return_value=""):
            with self.assertRaises(ValueError):
                package.verify(self.root, "keccak")


if __name__ == "__main__":
    unittest.main()
