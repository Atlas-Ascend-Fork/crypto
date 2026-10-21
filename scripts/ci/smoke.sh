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
# CI NPU 冒烟脚本。crypto-actions.yml 的 PreSmoke_A3 任务在带昇腾 NPU 的冒烟容器中、
# 检出的代码根目录下以 bash scripts/ci/smoke.sh 执行：编译全部组件并运行 op_kernel 层用例。
# 本脚本只负责准备 CI 环境并调用仓库根目录的 build.sh，构建与测试逻辑不在此处维护。
# 放在 .gitcode 之外便于 SIG 自行评审，稳定后迁移到 .gitcode/scripts/。
#
# 依赖的环境变量（由 crypto-actions.yml 设置）：
#   WORKSPACE      代码检出目录
#   TARGET_BRANCH  PR 目标分支

echo "WORKSPACE: ${WORKSPACE}"
echo "TARGET_BRANCH: ${TARGET_BRANCH}"

cd "${WORKSPACE}" || exit 1

# 冒烟镜像中 CANN 的安装路径与编译镜像不同
CANN_SETENV=/usr/local/Ascend/cann/set_env.sh
if [ ! -f "${CANN_SETENV}" ]; then
    echo "[ERROR] 未找到 ${CANN_SETENV}，冒烟镜像缺少 CANN 工具链"
    exit 1
fi
source "${CANN_SETENV}"

# PR 只改动 Markdown 文件时跳过冒烟
if [ -f "${WORKSPACE}/pr_filelist.txt" ]; then
    non_skip_count=$(grep -vE '\.md$' "${WORKSPACE}/pr_filelist.txt" | grep -cv '^$')
    if [ "${non_skip_count}" -eq 0 ]; then
        echo "pr_filelist.txt 仅包含 .md 文件，跳过 NPU 冒烟"
        exit 0
    fi
fi

# 设备状态仅用于排查，失败不阻断，设备是否可用以下方用例是否被跳过为准
npu-smi info || echo "[WARN] npu-smi 执行失败"

bash build.sh --pkg -j"$(nproc)"
ret=$?
if [ "${ret}" -ne 0 ]; then
    echo "[ERROR] build smoke is failed."
    exit 1
fi

bash build.sh --st 2>&1 | tee smoke_st.log
ret=${PIPESTATUS[0]}
if [ "${ret}" -ne 0 ]; then
    echo "[ERROR] device system tests failed."
    exit 1
fi

# 用例在 NPU 不可用或缺少依赖（如 pycryptodome）时会被 skip，pytest 仍返回成功。
# 在卡上运行时不应有用例被跳过，否则门禁会在用例未真正执行的情况下通过。
if grep -qE '[0-9]+ skipped' smoke_st.log; then
    echo "[ERROR] 有用例被跳过，原因见上方 SKIPPED 行，NPU 冒烟判为失败"
    exit 1
fi
echo "smoke is success."
exit 0
