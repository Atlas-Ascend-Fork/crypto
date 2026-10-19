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

# Build, prepare data, launch NTT, and verify its output.
set -euo pipefail
NTT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
SCRIPT_DIR="${NTT_DIR}/scripts"
STAGE=run
SOC_VERSION=Ascend910B1
JOBS="${NTT_BUILD_JOBS:-8}"
RUN_MODE=npu
BUILD_TYPE=Release
SDK_ARG=""

usage()
{
    printf 'Usage: bash %s [-r npu|sim] [-v SOC_VERSION] [-i CANN_SDK] [-b BUILD_TYPE] [-j JOBS]\n' "$0"
    printf '       [--profile ml-kem|ml-dsa|rns] [--stage run|build|install|package|env]\n'
    printf 'Default mode: npu. Host-only unit tests are built separately with CRYPTO_NTT_DEVICE=OFF.\n'
    printf 'KEM/DSA/RNS acceptance: bash %s/{kem,dsa,rns}_entry.sh\n' "${SCRIPT_DIR}"
    printf 'Local simulation prefers an installed CANN 8.5.0 SDK before latest. Explicit SDK environment variables take priority.\n'
    printf 'Set ASCEND_CANN_PACKAGE_PATH to select a complete, matching CANN SDK and simulator installation.\n'
}
while [[ $# -gt 0 ]]; do
    if [[ "$1" != -h && "$1" != --help && ( $# -lt 2 || -z "$2" ) ]]; then
        printf 'Missing value for %s\n' "$1" >&2
        exit 2
    fi
    case "$1" in
        -r|--run-mode|--mode) RUN_MODE="$2"; shift 2 ;;
        -v|--soc-version|--soc) SOC_VERSION="$2"; shift 2 ;;
        -i|--install-path) SDK_ARG="$2"; shift 2 ;;
        -b|--build-type) BUILD_TYPE="$2"; shift 2 ;;
        -j|--jobs) JOBS="$2"; shift 2 ;;
        --profile) export NTT_PROFILE="$2"; shift 2 ;;
        --stage) STAGE="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) printf 'Unsupported argument: %s\n' "$1" >&2; exit 2 ;;
    esac
done
case "${STAGE}" in run|build|install|package|env) ;; *) usage; exit 2 ;; esac
case "${BUILD_TYPE}" in Debug|Release|RelWithDebInfo|MinSizeRel) ;; *) usage; exit 2 ;; esac
export NTT_PROFILE="${NTT_PROFILE:-rns}"
case "${NTT_PROFILE}" in ml-kem|ml-dsa|rns) ;; *) printf 'Unsupported NTT profile: %s\n' "${NTT_PROFILE}" >&2; exit 2 ;; esac
case "${SOC_VERSION,,}" in
    ascend910b|ascend910b1) SOC_VERSION=Ascend910B1 ;;
    ascend910b2) SOC_VERSION=Ascend910B2 ;;
    ascend910b2c) SOC_VERSION=Ascend910B2C ;;
    ascend910b3) SOC_VERSION=Ascend910B3 ;;
    ascend910b4) SOC_VERSION=Ascend910B4 ;;
    ascend910_9362|ascend910_9372|ascend910_9381|ascend910_9382|ascend910_9391|ascend910_9392)
        SOC_VERSION="Ascend${SOC_VERSION:6}" ;;
    *) printf 'Kernel launch needs a concrete chip, e.g. Ascend910B1 or Ascend910_9382.\n' >&2; exit 2 ;;
esac
[[ "${JOBS}" =~ ^[1-9][0-9]*$ ]] || { usage; exit 2; }
[[ "${RUN_MODE}" == sim || "${RUN_MODE}" == npu ]] || { usage; exit 2; }
if [[ "${RUN_MODE}" == sim && "${SOC_VERSION}" != Ascend910B1 ]]; then
    printf 'Local Camodel validation supports Ascend910B1; use -r npu for other chips.\n' >&2
    exit 2
fi

load_cann()
{
    local candidate
    CANN_ROOT="${SDK_ARG:-${ASCEND_CANN_PACKAGE_PATH:-${ASCEND_INSTALL_PATH:-${ASCEND_HOME_PATH:-}}}}"
    if [[ -z "${CANN_ROOT}" ]]; then
        local candidates=()
        if [[ "${RUN_MODE}" == sim ]]; then
            candidates+=("${HOME}/Ascend/cann-8.5.0" /usr/local/Ascend/cann-8.5.0
                "${HOME}/Ascend/ascend-toolkit/8.5.0" /usr/local/Ascend/ascend-toolkit/8.5.0)
        fi
        candidates+=("${HOME}/Ascend/ascend-toolkit/latest" /usr/local/Ascend/ascend-toolkit/latest /usr/local/Ascend/latest)
        for candidate in "${candidates[@]}"; do
            if [[ -f "${candidate}/include/acl/acl.h" ]]; then CANN_ROOT="${candidate}"; break; fi
        done
    fi
    if [[ ! -f "${CANN_ROOT}/include/acl/acl.h" ]]; then
        printf 'Set ASCEND_HOME_PATH or ASCEND_CANN_PACKAGE_PATH to the CANN SDK.\n' >&2
        exit 2
    fi
    CANN_ROOT=$(cd "${CANN_ROOT}" && pwd -P)
    local env_file="${CANN_ROOT}/bin/setenv.bash"
    [[ -f "${env_file}" ]] || env_file="${CANN_ROOT}/set_env.sh"
    local env_result=0
    set +u
    source "${env_file}" || env_result=$?
    set -euo pipefail
    [[ "${env_result}" == 0 ]] || exit "${env_result}"
    export ASCEND_HOME_PATH="${CANN_ROOT}"
    export ASCEND_CANN_PACKAGE_PATH="${CANN_ROOT}"
    export CXX="${CXX:-c++}"
    PYTHON="${NTT_PYTHON:-python3}"
    PYTHON=$(command -v "${PYTHON}") || { printf 'Set NTT_PYTHON to an available Python interpreter.\n' >&2; exit 2; }
    CANN_CMAKE_DIR="${CANN_ROOT}/compiler/tikcpp/ascendc_kernel_cmake"
    if [[ ! -f "${CANN_CMAKE_DIR}/ascendc.cmake" ]]; then
        CANN_CMAKE_DIR="${CANN_ROOT}/tools/tikcpp/ascendc_kernel_cmake"
    fi
    SIM_LIB="${CANN_ROOT}/tools/simulator/Ascend910B1/lib"
}

set_build_paths()
{
    local tool_key
    tool_key=$({
        printf '%s\n' "${CANN_ROOT}" "${SOC_VERSION}" "${RUN_MODE}" "${BUILD_TYPE}" "${PYTHON}"
        "${PYTHON}" --version
        command -v "${CXX}"
        "${CXX}" --version
        cmake --version
        "${CANN_ROOT}/tools/bisheng_compiler/bin/bisheng" --version
        sha256sum "${CANN_CMAKE_DIR}/ascendc.cmake" "${CANN_CMAKE_DIR}/legacy_modules/function.cmake"
    } | sha256sum | cut -d ' ' -f 1)
    SOURCE_KEY=$({
        cd "${NTT_DIR}"
        find . -type d \( -name build -o -name __pycache__ -o -name CMakeFiles \) -prune -o \
            -type f ! -name README.md -print0 |
            sort -z | xargs -0 sha256sum
    } | sha256sum | cut -d ' ' -f 1)
    # SDK external projects rewrite intermediate objects in place; never reuse
    # those objects after source changes, even with a top-level clean target.
    BUILD_DIR="${NTT_DIR}/build/kernellaunch/${tool_key:0:16}-${SOURCE_KEY:0:16}"
    if [[ "${ACCEPT_FORCE_REBUILD:-0}" == 1 ]]; then
        mkdir -p "${NTT_DIR}/build/kernellaunch"
        BUILD_DIR=$(mktemp -d "${BUILD_DIR}.XXXXXX")
    fi
    EXAMPLE="${BUILD_DIR}/bin/ntt_kernellaunch_example"
}

build_device()
{
    mkdir -p "${BUILD_DIR}"
    local lock_fd
    exec {lock_fd}>"${BUILD_DIR}/.build.lock"
    flock "${lock_fd}"
    local old_key=""
    [[ ! -f "${BUILD_DIR}/.source_key" ]] || read -r old_key < "${BUILD_DIR}/.source_key"
    if [[ "${ACCEPT_FORCE_REBUILD:-0}" != 1 && "${old_key}" == "${SOURCE_KEY}" &&
          -x "${EXAMPLE}" && -f "${BUILD_DIR}/lib/libcann_crypto_ntt_device.so" &&
          -f "${BUILD_DIR}/lib/libntt_kernels.so" ]]; then
        printf '[REUSE] Kernel-launch build matches current sources and toolchain\n'
    else
        cmake -S "${NTT_DIR}" -B "${BUILD_DIR}" \
            -DCRYPTO_NTT_DEVICE=ON -DRUN_MODE="${RUN_MODE}" \
            -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
            -DASCEND_CANN_PACKAGE_PATH="${CANN_ROOT}" -DSOC_VERSION="${SOC_VERSION}" \
            -DASCEND_PYTHON_EXECUTABLE="${PYTHON}" \
            -DCMAKE_CXX_COMPILER="${CXX}" -DCMAKE_INSTALL_PREFIX="${BUILD_DIR}/install"
        cmake --build "${BUILD_DIR}" -j "${JOBS}"
        cmake --install "${BUILD_DIR}" --prefix "${BUILD_DIR}/install"
        printf '%s\n' "${SOURCE_KEY}" > "${BUILD_DIR}/.source_key"
    fi
    flock -u "${lock_fd}"
    exec {lock_fd}>&-
    printf '[BUILD] source=%s\n[BUILD] directory=%s\n' "${SOURCE_KEY}" "${BUILD_DIR}"
}

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
    build_device
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

load_cann
set_build_paths
case "${STAGE}" in
    build|install) build_device ;;
    package) build_device; (cd "${BUILD_DIR}" && cpack --config CPackConfig.cmake) ;;
    env) printf '[SDK] %s\n[MODE] %s\n[BUILD] %s\n[KERNEL LAUNCH] %s\n' "${CANN_ROOT}" "${RUN_MODE}" "${BUILD_DIR}" "${EXAMPLE}" ;;
    run) run_example ;;
esac
