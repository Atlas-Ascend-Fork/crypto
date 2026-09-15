#!/bin/bash
# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------
#
# CI 编译脚本。compile_action.yml 在检出的代码根目录下
# 以 BuildAccelerate bash scripts/ci/compile.sh 执行。
# 本脚本只负责准备 CI 环境并调用仓库根目录的 build.sh，构建逻辑不在此处维护。
# 放在 .gitcode 之外便于 SIG 自行评审，稳定后迁移到 .gitcode/scripts/。
#
# 依赖的环境变量（由 compile_action.yml 设置）：
#   WORKSPACE      代码检出目录
#   TARGET_BRANCH  PR 目标分支
#   task_name      编译任务名

echo "WORKSPACE: ${WORKSPACE}"
echo "TARGET_BRANCH: ${TARGET_BRANCH}"
echo "task_name: ${task_name}"

cd "${WORKSPACE}" || exit 1

CANN_SETENV=/home/jenkins/Ascend/cann/bin/setenv.bash
if [ ! -f "${CANN_SETENV}" ]; then
    echo "[ERROR] 未找到 ${CANN_SETENV}，CI 镜像缺少 CANN 工具链"
    exit 1
fi
source "${CANN_SETENV}"

# PR 只改动 Markdown 或 tests/ 下的文件时跳过编译
if [ -f "${WORKSPACE}/pr_filelist.txt" ]; then
    non_skip_count=$(grep -vE '(\.md$|^tests/)' "${WORKSPACE}/pr_filelist.txt" | grep -cv '^$')
    if [ "${non_skip_count}" -eq 0 ]; then
        echo "pr_filelist.txt 仅包含 .md 或 tests/ 下的文件，跳过编译"
        exit 0
    fi
fi

bash build.sh --pkg -j"$(nproc)"
ret=$?
if [ "${ret}" -ne 0 ]; then
    echo "[ERROR] build ${task_name} is failed."
    exit 1
fi
echo "build ${task_name} is success."
exit 0
