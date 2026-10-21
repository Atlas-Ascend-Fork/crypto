# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

import importlib.util
from pathlib import Path
import shutil
import subprocess
import sys

import pytest


def test_aes_gcm_aad_host_helper_matches_openssl(tmp_path):
    compiler = shutil.which("c++") or shutil.which("g++")
    if compiler is None:
        pytest.skip("a C++ compiler is required for the host helper test")
    if importlib.util.find_spec("torch") is None:
        pytest.skip("PyTorch headers and libraries are required for the host helper test")
    if not Path("/usr/include/openssl/evp.h").is_file():
        pytest.skip("OpenSSL development headers are required for the host helper test")

    torch_info = subprocess.run(
        [
            sys.executable,
            "-c",
            "import torch; from pathlib import Path; "
            "print(Path(torch.__file__).resolve().parent); "
            "print(int(torch._C._GLIBCXX_USE_CXX11_ABI))",
        ],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.splitlines()
    torch_root = Path(torch_info[0])
    abi = torch_info[1]
    torch_lib = torch_root / "lib"
    torch_extra_lib = torch_root.parent / "torch.libs"

    repo_root = Path(__file__).resolve().parents[3]
    source = Path(__file__).with_name("aes_gcm_aad_host_test.cpp")
    executable = tmp_path / "aes_gcm_aad_host_test"
    command = [
        compiler,
        "-std=c++17",
        "-O0",
        f"-D_GLIBCXX_USE_CXX11_ABI={abi}",
        f"-I{torch_root / 'include'}",
        f"-I{torch_root / 'include/torch/csrc/api/include'}",
        f"-I{repo_root}",
        str(source),
        f"-L{torch_lib}",
        f"-L{torch_extra_lib}",
        f"-Wl,-rpath,{torch_lib}",
        f"-Wl,-rpath,{torch_extra_lib}",
        "-ltorch",
        "-ltorch_cpu",
        "-lc10",
        "-lcrypto",
        "-o",
        str(executable),
    ]
    subprocess.run(command, check=True, capture_output=True, text=True)
    subprocess.run([str(executable)], check=True, capture_output=True, text=True)
