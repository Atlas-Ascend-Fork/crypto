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

# Execute an already-built NTT example and compare its complete output.
set -euo pipefail
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
NTT_DIR=$(cd "${SCRIPT_DIR}/.." && pwd)
EXAMPLE="$1"
CANN_ROOT="$2"
RUN_MODE="$3"
export CANN_CRYPTO_SOC_TARGET="$4"
BUILD_DIR=$(cd "$(dirname "${EXAMPLE}")/.." && pwd)
SIM_LIB="${CANN_ROOT}/tools/simulator/Ascend910B1/lib"
PYTHON="${NTT_PYTHON:-python3}"
export NTT_PROFILE="${NTT_PROFILE:-rns}"

activate_runtime()
{
    unset ASCEND_CUSTOM_OPP_PATH
    export LD_LIBRARY_PATH="${BUILD_DIR}/lib:${CANN_ROOT}/lib64:${LD_LIBRARY_PATH:-}"
    if [[ "${RUN_MODE}" == sim ]]; then
        export LD_LIBRARY_PATH="${BUILD_DIR}/simulator_compat:${SIM_LIB}:${LD_LIBRARY_PATH}"
        local linkage
        linkage=$(ldd -r "${EXAMPLE}" 2>&1) || { printf '%s\n' "${linkage}" >&2; return 1; }
        if grep -Eq 'undefined symbol:|=> not found' <<< "${linkage}"; then
            printf '%s\n' "${linkage}" | grep -E 'undefined symbol:|=> not found' >&2
            printf 'CANN host libraries and Camodel are incompatible in %s. Select a matching SDK with ASCEND_CANN_PACKAGE_PATH.\n' "${CANN_ROOT}" >&2
            return 1
        fi
    fi
    export ASCEND_SLOG_PRINT_TO_STDOUT="${ASCEND_SLOG_PRINT_TO_STDOUT:-0}"
    export ASCEND_GLOBAL_LOG_LEVEL="${ASCEND_GLOBAL_LOG_LEVEL:-3}"
    export ASCEND_GLOBAL_EVENT_ENABLE="${ASCEND_GLOBAL_EVENT_ENABLE:-0}"
    printf '[KERNEL LAUNCH] %s\n' "${EXAMPLE}"
}

prepare_data()
{
    if [[ "${NTT_PROFILE}" == rns ]]; then
        NTT_N="${NTT_N:-65536}" NTT_BLOCK_DIM="${NTT_BLOCK_DIM:-16}" NTT_W="${NTT_W:-32}" \
        NTT_CHANNEL_BLOCK="${NTT_CHANNEL_BLOCK:-1}" NTT_RNS_CHANNELS="${NTT_RNS_CHANNELS:-0}" \
        NTT_LOGP="${NTT_LOGP:-200}" "${PYTHON}" "${SCRIPT_DIR}/gen_data.py"
    else
        local kernel=mlkem-poly
        [[ "${NTT_PROFILE}" != ml-dsa ]] || kernel=mldsa-poly
        PQC_KERNEL="${kernel}" PQC_OPERATION=ntt PQC_BATCH="${NTT_BENCH}" \
        PQC_SEED="${NTT_SEED:-910203}" NTT_DATA_DIR="${PWD}" \
            "${PYTHON}" "${SCRIPT_DIR}/gen_pqc_data.py"
    fi
}

verify_result()
{
    if [[ "${NTT_PROFILE}" == rns ]]; then
        "${PYTHON}" "${SCRIPT_DIR}/compare_data.py" output/output_z.bin output/golden.bin
    else
        local kernel=mlkem-poly
        [[ "${NTT_PROFILE}" != ml-dsa ]] || kernel=mldsa-poly
        PQC_KERNEL="${kernel}" PQC_OPERATION=ntt \
            "${PYTHON}" "${SCRIPT_DIR}/compare_pqc_data.py" output/output_z.bin output/golden.bin
    fi
}

run_example()
{
    activate_runtime
    local run_dir="${NTT_RUN_DIR:-}"
    if [[ -z "${run_dir}" ]]; then
        mkdir -p "${NTT_DIR}/build/acceptance"
        run_dir=$(mktemp -d "${NTT_DIR}/build/acceptance/${NTT_PROFILE:-rns}.XXXXXX")
    fi
    mkdir -p "${run_dir}/plog"
    run_dir=$(cd "${run_dir}" && pwd)
    export ASCEND_PROCESS_LOG_PATH="${ASCEND_PROCESS_LOG_PATH:-${run_dir}/plog}"
    local default_bench=1
    case "${NTT_PROFILE:-rns}" in ml-kem|ml-dsa) default_bench=4096 ;; esac
    export NTT_BENCH="${NTT_BENCH:-${default_bench}}"
    printf '[RUN] mode=%s\n[RUN] directory=%s\n' "${RUN_MODE}" "${run_dir}"
    if [[ "${RUN_MODE}" == sim ]]; then
        mkdir -p "${run_dir}/camodel"
        export CAMODEL_LOG_PATH="${CAMODEL_LOG_PATH:-${run_dir}/camodel}"
        export CAMODEL_CONFIG_PATH="${CAMODEL_CONFIG_PATH:-${SCRIPT_DIR}/camodel_config}"
        cp "${CAMODEL_CONFIG_PATH}/config.json" "${run_dir}/camodel_config.json"
        printf '[RUN] model_config=%s\n' "${CAMODEL_CONFIG_PATH}"
    fi
    (cd "${run_dir}" && prepare_data && "${EXAMPLE}" && verify_result) 2>&1 | tee "${run_dir}/run.log"
}

run_example
