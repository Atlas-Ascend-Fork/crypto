# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
import importlib.util
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'src/primitives/keccak/cmake/stage.py'
SPEC = importlib.util.spec_from_file_location('stage_under_test', SOURCE)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


@pytest.mark.parametrize('exists', [False, True])
def test_explicit_module_requires_actual_operator_sources(tmp_path, exists):
    module = tmp_path / 'module'
    if exists:
        module.mkdir()
    with pytest.raises(ValueError):
        MODULE.stage(tmp_path / 'output', [module])
    assert not (tmp_path / 'output').exists()


def test_real_operator_sources_can_still_be_staged(tmp_path):
    output = tmp_path / 'output'
    MODULE.stage(output, [ROOT / 'src/primitives/keccak'])
    assert (output / 'CMakeLists.txt').is_file()
    assert (output / 'op_host/keccak_f1600_tensor_def.cpp').is_file()
