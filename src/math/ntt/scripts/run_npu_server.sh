#!/usr/bin/env bash
# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

# Compatibility hook used by the shared repository build.sh.
set -euo pipefail
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
STAGE="${1:-plan}"
if [[ $# -gt 0 ]]; then shift; fi
case "${STAGE}" in
    plan|-h|--help) exec bash "${SCRIPT_DIR}/../run.sh" --help ;;
    kem-entry|dsa-entry|rns-entry) exec bash "${SCRIPT_DIR}/${STAGE%-entry}_entry.sh" "$@" ;;
    npu-test|test) STAGE=run; set -- -r npu "$@" ;;
    sim-test) STAGE=run; set -- -r sim "$@" ;;
    build|install|package|env) ;;
    *) printf 'Unsupported stage: %s\n' "${STAGE}" >&2; exit 2 ;;
esac
exec bash "${SCRIPT_DIR}/../run.sh" --stage "${STAGE}" "$@"
