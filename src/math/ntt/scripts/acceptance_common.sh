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

set -euo pipefail

ENTRY_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
NTT_DIR=$(cd "${ENTRY_DIR}/.." && pwd)
REPO_ROOT=$(cd "${NTT_DIR}/../../.." && pwd)
BASELINE_FILE="${NTT_DIR}/tests/ut/op_kernel/ntt_data/acceptance_baselines.tsv"
SERVER_SCRIPT="${NTT_DIR}/scripts/run_npu_server.sh"
ACCEPT_BATCH="${ACCEPT_BATCH:-4096}"
ACCEPT_SOC="${ACCEPT_SOC:-ascend910b}"
ACCEPT_VENDOR="${ACCEPT_VENDOR:-cann_crypto}"

baseline_value()
{
    local profile="$1"
    local backend="$2"
    awk -F '\t' -v p="${profile}" -v b="${backend}" \
        '$1 == p && $2 == b {print $5; found=1} END {if (!found) exit 1}' \
        "${BASELINE_FILE}"
}

ensure_acceptance_package()
{
    "${SERVER_SCRIPT}" build --mode sim --soc "${ACCEPT_SOC}" --vendor "${ACCEPT_VENDOR}"
}

run_sim_profile()
{
    local profile="$1"
    local batch="$2"
    local stamp
    stamp=$(date '+%Y%m%d_%H%M%S_%N')
    ACCEPT_LOG_ROOT="${REPO_ROOT}/build/ntt_acceptance/${profile}_${stamp}"
    mkdir -p "${ACCEPT_LOG_ROOT}/camodel" "${ACCEPT_LOG_ROOT}/plog"
    touch "${ACCEPT_LOG_ROOT}/start.marker"

    CAMODEL_LOG_PATH="${ACCEPT_LOG_ROOT}/camodel" \
    ASCEND_PROCESS_LOG_PATH="${ACCEPT_LOG_ROOT}/plog" \
    NTT_PROFILE="${profile}" NTT_BENCH="${batch}" \
    NTT_RUN_DIR="${ACCEPT_LOG_ROOT}" \
        "${SERVER_SCRIPT}" sim-test --soc "${ACCEPT_SOC}" --vendor "${ACCEPT_VENDOR}"
}

find_task_toml()
{
    find "${ACCEPT_LOG_ROOT}/camodel" -type f -name 'profile_task_log*.toml' -print -quit
}

run_pqc_acceptance()
{
    local profile="$1"
    local q="$2"
    local parameter_sets="$3"
    ensure_acceptance_package
    echo "[ACCEPT] profile=${profile} parameter_sets=${parameter_sets} N=256 q=${q} batch=${ACCEPT_BATCH}"
    run_sim_profile "${profile}" "${ACCEPT_BATCH}"

    local toml
    toml=$(find_task_toml)
    if [[ -z "${toml}" || ! -f "${toml}" ]]; then
        echo "[FAIL] simulator task TOML was not generated" >&2
        return 1
    fi
    local duration
    duration=$(awk -F ' = ' '/^[[:space:]]*duration =/{print $2; exit}' "${toml}")
    if ! [[ "${duration}" =~ ^[0-9]+$ ]]; then
        echo "[FAIL] invalid task duration in ${toml}" >&2
        return 1
    fi

    local liboqs
    local fasterntt
    liboqs=$(baseline_value "${profile}" liboqs-avx2)
    fasterntt=$(baseline_value "${profile}" fasterntt-avx2-reference)
    python3 - "${profile}" "${duration}" "${ACCEPT_BATCH}" \
        "${liboqs}" "${fasterntt}" "${toml}" "${ACCEPT_ENFORCE_PERFORMANCE:-1}" <<'PY'
import math
import sys

profile, duration, batch, liboqs, fasterntt, toml, enforce = sys.argv[1:]
duration = int(duration)
batch = int(batch)
liboqs = float(liboqs)
fasterntt = float(fasterntt)
for baseline in (liboqs, fasterntt):
    if not math.isfinite(baseline):
        raise SystemExit("[FAIL] baseline cycles_per_poly must be finite and positive")
    if baseline <= 0:
        raise SystemExit("[FAIL] baseline cycles_per_poly must be finite and positive")
npu = duration / batch
speed_libo = liboqs / npu
speed_fntt = fasterntt / npu
print(f"[METRIC] {profile} toml_duration={duration} batch={batch} npu_cycles_per_poly={npu:.6f}")
print(f"[COMPARE] liboqs_avx2={liboqs:.6f} speedup={speed_libo:.3f}x required=6.000x")
print(f"[COMPARE] fasterntt_avx2_reference={fasterntt:.6f} speedup={speed_fntt:.3f}x required=6.000x")
print(f"[EVIDENCE] {toml}")
if speed_libo < 6.0 or speed_fntt < 6.0:
    if enforce != "0":
        raise SystemExit(f"[FAIL] {profile} NTT performance gate")
    print(f"[INFO] {profile} performance gate not enforced for this run")
    print(f"[PASS] {profile} correctness and batch processing")
else:
    print(f"[PASS] {profile} correctness, batch processing, and performance gates")
PY
}

run_rns_acceptance()
{
    ensure_acceptance_package
    local block_dim=16
    local channel_block=1
    local sub_bench=16

    local stamp
    stamp=$(date '+%Y%m%d_%H%M%S_%N')
    ACCEPT_LOG_ROOT="${REPO_ROOT}/build/ntt_acceptance/rns_${stamp}"
    mkdir -p "${ACCEPT_LOG_ROOT}/camodel" "${ACCEPT_LOG_ROOT}/plog"
    touch "${ACCEPT_LOG_ROOT}/start.marker"

    echo "[ACCEPT] RNS N=65536 logN=16 logP=200 blockDim=${block_dim} channelBlock=${channel_block} subBench=${sub_bench}"
    CAMODEL_LOG_PATH="${ACCEPT_LOG_ROOT}/camodel" \
    ASCEND_PROCESS_LOG_PATH="${ACCEPT_LOG_ROOT}/plog" \
    NTT_PROFILE=rns NTT_N=65536 NTT_LOGP=200 \
    NTT_RNS_CHANNELS=0 NTT_BLOCK_DIM="${block_dim}" \
    NTT_CHANNEL_BLOCK="${channel_block}" NTT_W="${sub_bench}" NTT_BENCH=1 \
    NTT_RUN_DIR="${ACCEPT_LOG_ROOT}" \
        "${SERVER_SCRIPT}" sim-test --soc "${ACCEPT_SOC}" --vendor "${ACCEPT_VENDOR}"

    local toml
    toml=$(find_task_toml)
    if [[ -z "${toml}" || ! -f "${toml}" ]]; then
        echo "[FAIL] simulator task TOML was not generated" >&2
        return 1
    fi
    local duration
    duration=$(awk -F ' = ' '/^[[:space:]]*duration =/{print $2; exit}' "${toml}")
    if ! [[ "${duration}" =~ ^[0-9]+$ ]]; then
        echo "[FAIL] invalid task duration in ${toml}" >&2
        return 1
    fi
    echo "[METRIC] rns toml_duration=${duration}"
    echo "[EVIDENCE] ${toml}"

    echo "[PASS] RNS formal contract: logN=16 logP=200 correctness"
}
