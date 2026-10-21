# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.

"""Exercise the real runner and CTest with independent Host, Kernel UT and ST cases."""
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[3]


@pytest.fixture
def project(tmp_path):
    shutil.copy2(ROOT / "build.sh", tmp_path / "build.sh")
    component = tmp_path / "src/math/fixture"
    (component / "tests/ut/op_kernel").mkdir(parents=True)
    (component / "tests/CMakeLists.txt").touch()
    (component / "tests/ut/op_kernel/CMakeLists.txt").touch()
    (component / "CMakeLists.txt").write_text('''
cmake_minimum_required(VERSION 3.16)
project(test_selection NONE)
enable_testing()
option(CANN_BUILD_TESTS "Tests" ON)
option(CANN_BUILD_DEVICE "Device tests" OFF)
option(CANN_BUILD_CPU_DEBUG_TESTS "CPU kernel tests" OFF)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
function(register_case name label)
    add_test(NAME ${name} COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/case.py" ${name})
    set_tests_properties(${name} PROPERTIES LABELS ${label})
endfunction()
register_case(host host)
if(CANN_BUILD_CPU_DEBUG_TESTS)
    register_case(kernel kernel_ut)
endif()
if(CANN_BUILD_DEVICE)
    register_case(device device)
endif()
''')
    (component / "case.py").write_text('''
from pathlib import Path
import sys
root = Path(__file__).parent
(root / (sys.argv[1] + ".ran")).touch()
raise SystemExit(1 if (root / "fail").exists() else 0)
''')
    return tmp_path


def run_tests(project, arguments, with_cann=True):
    env = os.environ.copy()
    env.pop("ASCEND_HOME_PATH", None)
    if with_cann:
        # The fixture tests dispatch only; it neither compiles nor runs CANN code.
        env["ASCEND_HOME_PATH"] = str(project / "sdk")
    return subprocess.run(
        [str(project / "build.sh"), "--components=math/fixture", "-j1", *arguments],
        env=env, text=True, capture_output=True, timeout=60, check=False,
    )


@pytest.mark.parametrize("arguments,expected", [
    (["-u", "--ophost"], {"host"}),
    (["-u", "--opkernel"], {"kernel"}),
    (["--st"], {"device"}),
    (["-u"], {"host", "kernel"}),
])
def test_only_requested_scope_executes(project, arguments, expected):
    result = run_tests(project, arguments)
    assert result.returncode == 0, result.stdout + result.stderr
    assert {p.stem for p in (project / "src/math/fixture").glob("*.ran")} == expected


@pytest.mark.parametrize("arguments", [["-u", "--opkernel"], ["--st"]])
def test_missing_sdk_is_failure_even_when_empty_allowed(project, arguments):
    result = run_tests(project, [*arguments, "--allow_empty"], with_cann=False)
    assert result.returncode != 0, result.stdout + result.stderr
    assert not list((project / "src/math/fixture").glob("*.ran"))


@pytest.mark.parametrize("arguments", [["-u", "--ophost"], ["-u", "--opkernel"], ["--st"]])
def test_selected_test_failure_is_propagated(project, arguments):
    (project / "src/math/fixture/fail").touch()
    result = run_tests(project, [*arguments, "--allow_empty"])
    assert result.returncode != 0, result.stdout + result.stderr
    assert list((project / "src/math/fixture").glob("*.ran"))
