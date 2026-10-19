# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
"""Check the complete real Host heredoc and TSV loader, without device calls."""
import csv
import io
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[3]
SCRIPT = ROOT / "src/math/ntt/scripts/acceptance_common.sh"
BASELINES = ROOT / "src/math/ntt/scripts/acceptance_baselines.tsv"


def complete_host_payload():
    matches = re.findall(r"<<'PY'\n(.*?)\nPY", SCRIPT.read_text(encoding="utf-8"), re.S)
    assert len(matches) == 1
    return matches[0] + "\n"


def load_actual_baseline(path, profile, backend):
    # Only source the definitions and execute the existing TSV loader. No
    # package, simulator, device function, or API is replaced or invoked.
    bash_path = shutil.which("bash")
    assert bash_path is not None, "Required Host command not found: bash"
    assert Path(bash_path).is_absolute(), bash_path
    with tempfile.TemporaryDirectory(prefix="acceptance-tsv-driver-") as directory:
        driver = Path(directory) / "driver.sh"
        driver.write_text(
            '#!/usr/bin/env bash\n'
            'set -euo pipefail\n'
            'source "$1"\n'
            'BASELINE_FILE="$2"\n'
            'baseline_value "$3" "$4"\n',
            encoding="utf-8",
        )
        return subprocess.run(
            [bash_path, str(driver), str(SCRIPT), str(path), profile, backend],
            capture_output=True, text=True, timeout=15, check=False,
        )


def execute_payload(path, profile, duration, enforce):
    values = []
    for backend in ("liboqs-avx2", "fasterntt-avx2-reference"):
        loaded = load_actual_baseline(path, profile, backend)
        assert loaded.returncode == 0, loaded.stderr
        values.append(loaded.stdout.strip())
    return subprocess.run(
        [sys.executable, "-", profile, str(duration), "4096", *values,
         "host-argument-fixture.toml", enforce],
        input=complete_host_payload(), capture_output=True, text=True,
        timeout=15, check=False,
    )


@pytest.mark.parametrize("value", ["nan", "inf", "0", "-1"])
@pytest.mark.parametrize("backend", ["liboqs-avx2", "fasterntt-avx2-reference"])
@pytest.mark.parametrize("enforce", ["1", "0"])
def test_invalid_baseline_is_not_a_performance_or_correctness_pass(tmp_path, value, backend, enforce):
    rows = list(csv.reader(io.StringIO(BASELINES.read_text(encoding="utf-8")), delimiter="\t"))
    changed = 0
    for row in rows:
        if row[0] == "ml-kem" and row[1] == backend:
            row[4] = value
            changed += 1
    assert changed == 1
    fixture = tmp_path / "synthetic erroneous baseline.tsv"
    with fixture.open("w", encoding="utf-8", newline="") as stream:
        csv.writer(stream, delimiter="\t", lineterminator="\n").writerows(rows)
    result = execute_payload(fixture, "ml-kem", 4096, enforce)
    assert result.returncode != 0, result.stdout
    assert "baseline cycles_per_poly must be finite and positive" in result.stderr
    assert "[PASS]" not in result.stdout


@pytest.mark.parametrize("profile", ["ml-kem", "ml-dsa"])
@pytest.mark.parametrize("duration,enforce,expected", [
    (4096, "1", 0), (4096, "0", 0),
    (40960000, "1", 1), (40960000, "0", 0),
])
def test_legal_baselines_keep_threshold_and_diagnostic_only_policy(profile, duration, enforce, expected):
    result = execute_payload(BASELINES, profile, duration, enforce)
    assert result.returncode == expected, result.stdout + result.stderr
    if duration == 40960000 and enforce == "0":
        assert "performance gate not enforced" in result.stdout
        assert "correctness and batch processing" in result.stdout
    if duration == 40960000 and enforce == "1":
        assert "[FAIL]" in result.stderr


@pytest.mark.parametrize("profile,backend", [
    ("ml-kem", "liboqs-avx2"), ("ml-kem", "fasterntt-avx2-reference"),
    ("ml-dsa", "liboqs-avx2"), ("ml-dsa", "fasterntt-avx2-reference"),
])
def test_actual_tsv_records_are_finite_positive(profile, backend):
    import math

    result = load_actual_baseline(BASELINES, profile, backend)
    assert result.returncode == 0
    value = float(result.stdout.strip())
    assert math.isfinite(value)
    assert value > 0
