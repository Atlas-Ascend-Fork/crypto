#!/usr/bin/env python3
"""标准算子工程生成与包接口检查的单元测试。"""

import importlib.util
import os
import sys
from pathlib import Path
import tempfile
from types import SimpleNamespace
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

# 编译命令单元测试不加载 CANN，外部工具调用由测试替身记录。
with patch.dict(sys.modules, {
    "const_var": SimpleNamespace(WFLAGS=os.O_WRONLY | os.O_CREAT | os.O_TRUNC, WMODES=0o600),
    "ascendc_impl_build": SimpleNamespace(),
    "ascendc_bin_param_build": SimpleNamespace(),
    "ascendc_op_info": SimpleNamespace(OpInfo=lambda *args: SimpleNamespace(get_op_file=lambda: "test_op")),
}):
    compiler = load("kernel_compiler", MODULE / "cmake/ascend/cmake/util/ascendc_compile_kernel.py")


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
        self.assertEqual(source.read_bytes(), (MODULE / "op_kernel/keccak_f1600_u32_vector.h").read_bytes())

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
        content = (output / "op_kernel/shake_uniform_u32_tensor.h").read_text(encoding="utf-8")
        self.assertIn('#include "keccak_f1600_u32_vector.h"', content)

    def make_builder(self):
        args = SimpleNamespace(op_name="TestOp", src_file=str(self.root / "test.cpp"),
            compute_unit="ascend910b", compile_options="", debug_config="",
            config_ini=str(self.root / "test.ini"), tiling_lib=str(self.root / "tiling.so"),
            output_path=str(self.root / "binary"), dynamic_dir=None, json_file=None)
        with patch.object(compiler.os, "getcwd", return_value=str(self.root)):
            return compiler.CompileKernel(args)

    def test_compile_retry_has_independent_directory(self):
        first = self.make_builder()
        first.op_impl_py = str(self.root / "test.py")
        with patch.object(compiler.subprocess, "call", return_value=1):
            with self.assertRaises(RuntimeError):
                first.ascendc_build()
        second = self.make_builder()
        self.assertNotEqual(first.working_dir, second.working_dir)
        self.assertTrue(Path(first.working_dir).is_dir())
        self.assertTrue(Path(second.working_dir).is_dir())
        second.clean()
        second.clean()
        self.assertTrue(Path(first.working_dir).is_dir())

    def test_compile_uses_selected_python(self):
        builder = self.make_builder()
        builder.op_impl_py = str(self.root / "test.py")
        with patch.object(compiler.subprocess, "call", return_value=0) as call:
            builder.ascendc_build()
        self.assertEqual(call.call_args.kwargs["env"]["HI_PYTHON"], sys.executable)
        self.assertEqual(call.call_args.kwargs["env"]["ASCEND_CUSTOM_OPP_PATH"], builder.build_opp_path)
        self.assertEqual(call.call_args.args[0][0], "make")
        builder.clean()

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
