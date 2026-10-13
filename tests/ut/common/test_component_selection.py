# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.

import os
from pathlib import Path
import shutil
import subprocess

import pytest

REPOSITORY_ROOT = Path(__file__).resolve().parents[3]


@pytest.fixture
def project(tmp_path):
    shutil.copy2(REPOSITORY_ROOT / "build.sh", tmp_path / "build.sh")
    for component in ("math/valid", "algorithms/hash/deep"):
        root = tmp_path / "src" / component
        root.mkdir(parents=True)
        (root / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.16)\n"
            "project(component_fixture LANGUAGES CXX)\n"
            "add_executable(component_smoke smoke.cpp)\n",
            encoding="utf-8",
        )
        (root / "smoke.cpp").write_text("int main() { return 0; }\n", encoding="utf-8")
    return tmp_path


def run_build(project, *arguments):
    env = os.environ.copy()
    env.pop("ASCEND_HOME_PATH", None)
    return subprocess.run(
        [str(project / "build.sh"), "--pkg", "-j1", *arguments],
        cwd=project, env=env, text=True, capture_output=True, timeout=60,
        check=False,
    )


@pytest.mark.parametrize("component", [
    "math/missing", "missing", "algorithms/hash/missing",
])
def test_unknown_component_fails_before_build(project, component):
    result = run_build(project, f"--components={component}")
    assert result.returncode != 0, result.stdout
    assert not (project / "build_out").exists()
    assert "开始编译" not in result.stdout


def test_directory_without_build_entry_fails(project):
    result = run_build(project, "--components=math")
    assert result.returncode != 0, result.stdout
    assert not (project / "build_out").exists()


@pytest.mark.parametrize("components", [
    "math/valid,math/missing", "math/missing,math/valid",
])
def test_mixed_selection_fails_before_any_component_build(project, components):
    result = run_build(project, f"--components={components}")
    assert result.returncode != 0, result.stdout
    assert not (project / "src/math/valid/build").exists()
    assert not (project / "build_out").exists()


@pytest.mark.parametrize("components", [
    "", ",math/valid", "math/valid,", "math/valid,,algorithms/hash/deep",
])
def test_empty_explicit_component_fails(project, components):
    result = run_build(project, f"--components={components}")
    assert result.returncode != 0, result.stdout
    assert not (project / "build_out").exists()


@pytest.mark.parametrize("component", ["math/valid", "algorithms/hash/deep"])
def test_valid_component_builds_with_real_cmake_and_compiler(project, component):
    source = project / "src" / component / "smoke.cpp"
    before = source.read_bytes()
    result = run_build(project, f"--components={component}")
    assert result.returncode == 0, result.stdout + result.stderr
    subprocess.run(
        [str(source.parent / "build/component_smoke")], check=True, timeout=10
    )
    assert source.read_bytes() == before


def test_default_discovery_builds_both_components(project):
    result = run_build(project)
    assert result.returncode == 0, result.stdout + result.stderr
    for component in ("math/valid", "algorithms/hash/deep"):
        subprocess.run(
            [str(project / "src" / component / "build/component_smoke")],
            check=True, timeout=10,
        )
