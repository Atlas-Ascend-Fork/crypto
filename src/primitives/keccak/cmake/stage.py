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

"""为 CANN 标准自定义算子编译生成输入工程。"""

import argparse
from pathlib import Path
import re


def rewrite_includes(source, owned):
    def rewrite(match):
        include = match.group(1)
        resolved = (source.parent / include).resolve()
        return '#include "' + (resolved.name if resolved in owned else include) + '"'
    return re.sub(r'#include\s+"([^"]+)"', rewrite, source.read_text(encoding="utf-8"))


def stage(output, modules):
    output = output.resolve()
    roots = [module.resolve() for module in modules]
    if any(output == root or output in root.parents for root in roots):
        raise ValueError("生成目录不能覆盖源码目录")
    marker = output / ".cann-generated"
    if output.exists() and (not marker.is_file() or marker.read_text() != "cann-crypto-sdk\n"):
        raise ValueError("请使用新的构建目录")
    sources = []
    for module in roots:
        for folder in ("op_host", "op_kernel"):
            sources.extend((source, folder) for source in sorted((module / folder).glob("*"))
                           if source.suffix in (".cpp", ".h"))
        sources.extend((source, "op_host") for source in sorted(module.glob("*_internal.h")))
        sources.extend((source, "op_host") for source in sorted(module.glob("*_launch.cpp")))
    owned = {source.resolve() for source, _ in sources}
    targets = [output / folder / source.name for source, folder in sources]
    if len(targets) != len(set(targets)):
        raise ValueError("算子源文件名称冲突")
    for folder in ("op_host", "op_kernel"):
        for old in (output / folder).glob("*"):
            if old.suffix in (".cpp", ".h") and old not in targets:
                raise ValueError("生成目录中存在过期文件，请指定新的构建目录")
    if any(target.is_symlink() or target.parent.is_symlink() for target in targets):
        raise ValueError("生成路径不能为符号链接")
    output.mkdir(parents=True, exist_ok=True)
    marker.write_text("cann-crypto-sdk\n", encoding="utf-8")
    template = Path(__file__).resolve().parent / "device/CMakeLists.txt"
    write_if_changed(output / "CMakeLists.txt", template.read_text(encoding="utf-8"))
    for (source, _), target in zip(sources, targets):
        content = rewrite_includes(source, owned)
        write_if_changed(target, content)
    print("Ascend C project: {} ({} source files)".format(output, len(sources)))


def write_if_changed(target, content):
    target.parent.mkdir(parents=True, exist_ok=True)
    # 内容不变时保留时间戳，避免重复编译。
    if not target.exists() or target.read_text(encoding="utf-8") != content:
        with target.open("w", encoding="utf-8", newline="\n") as stream:
            stream.write(content)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--module", type=Path, action="append", required=True)
    args = parser.parse_args()
    try:
        stage(args.output, args.module)
    except (OSError, ValueError) as error:
        parser.exit(1, "{}\n".format(error))
