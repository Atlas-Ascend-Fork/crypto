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

"""检查标准算子包中的 ACLNN 头文件和导出符号。"""

import argparse
from pathlib import Path
import subprocess


def verify(package, module):
    operators = [("keccak_f1600_tensor", "KeccakF1600Tensor")]
    if module == "shake":
        operators += [("shake128_tensor", "Shake128Tensor"), ("shake256_tensor", "Shake256Tensor")]
    for stem, _ in operators:
        header = package / "op_api/include" / ("aclnn_" + stem + ".h")
        if not header.is_file():
            raise ValueError("缺少头文件：{}".format(header))
    symbols = subprocess.check_output(
        ["nm", "-D", "--defined-only", str(package / "op_api/lib/libcust_opapi.so")], text=True)
    exported = {line.split()[-1].split("@")[0] for line in symbols.splitlines() if line.split()}
    expected = {"aclnn" + name + suffix for _, name in operators for suffix in ("", "GetWorkspaceSize")}
    if expected - exported:
        raise ValueError("缺少导出符号：{}".format(sorted(expected - exported)))
    print("PACKAGE_SYMBOLS=PASS")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("--module", choices=("keccak", "shake"), required=True)
    args = parser.parse_args()
    try:
        verify(args.package, args.module)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, "{}\n".format(error))
