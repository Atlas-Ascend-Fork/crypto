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
# CI 单元测试脚本。llt_action.yml 在检出的代码根目录下
# 以 bash scripts/ci/ut.sh <ut_type> 执行。
# 本脚本只负责准备 CI 环境并调用仓库根目录的 build.sh，测试逻辑不在此处维护。
# 放在 .gitcode 之外便于 SIG 自行评审，稳定后迁移到 .gitcode/scripts/。
#
# 参数：
#   $1（或环境变量 ut_type）  UT 层次：ophost、opapi、opgraph、opkernel
# 依赖的环境变量（由 llt_action.yml 设置）：
#   WORKSPACE      代码检出目录
#   TARGET_BRANCH  PR 目标分支

ut_type="${1:-${ut_type}}"
echo "WORKSPACE: ${WORKSPACE}"
echo "TARGET_BRANCH: ${TARGET_BRANCH}"
echo "ut_type: ${ut_type}"

cd "${WORKSPACE}" || exit 1

CANN_SETENV=/home/jenkins/Ascend/cann/bin/setenv.bash
if [ ! -f "${CANN_SETENV}" ]; then
    echo "[ERROR] 未找到 ${CANN_SETENV}，CI 镜像缺少 CANN 工具链"
    exit 1
fi
source "${CANN_SETENV}"

# PR 只改动 Markdown 文件时跳过 UT
if [ -f "${WORKSPACE}/pr_filelist.txt" ]; then
    non_skip_count=$(grep -vE '\.md$' "${WORKSPACE}/pr_filelist.txt" | grep -cv '^$')
    if [ "${non_skip_count}" -eq 0 ]; then
        echo "pr_filelist.txt 仅包含 .md 文件，跳过 UT"
        exit 0
    fi
fi

case "${ut_type}" in
    ophost|opapi|opgraph|opkernel)
        bash build.sh -u --"${ut_type}"
        ret=$?
        ;;
    *)
        echo "[ERROR] 不支持的 ut_type：${ut_type}，可选 ophost、opapi、opgraph、opkernel"
        exit 1
        ;;
esac

if [ "${ret}" -ne 0 ]; then
    echo "[ERROR] run ut ${ut_type} is failed."
    exit 1
fi
echo "run ut ${ut_type} is success."
exit 0
