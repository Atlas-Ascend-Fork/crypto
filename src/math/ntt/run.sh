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
COMPUTE_UNIT="${ASCEND_COMPUTE_UNIT:-ascend910b}"
JOBS="${NTT_BUILD_JOBS:-8}"
RUN_MODE=npu
BUILD_TYPE=Release
SDK_ARG=""

usage()
{
    printf 'Usage: bash %s [-r npu|sim] [-v ascend910b|ascend910_93] [-i CANN_SDK] [-b BUILD_TYPE] [-j JOBS]\n' "$0"
    printf '       [--profile ml-kem|ml-dsa|rns] [--stage run|build|install|package|env]\n'
    printf 'Default mode: npu. Host-only unit tests are built separately with CANN_BUILD_DEVICE=OFF.\n'
    printf 'Default target: ascend910b (A2); use ascend910_93 for A3. Both compile for dav-2201.\n'
    printf 'Local simulation uses the Ascend910B1 model.\n'
    printf 'KEM/DSA/RNS acceptance: bash %s/{kem,dsa,rns}_entry.sh\n' "${SCRIPT_DIR}"
    printf 'NPU and simulation use CANN 9.0.0 by default. Explicit SDK environment variables take priority.\n'
    printf 'Set ASCEND_CANN_PACKAGE_PATH to select a complete, matching CANN SDK and simulator installation.\n'
}
while [[ $# -gt 0 ]]; do
    if [[ "$1" != -h && "$1" != --help && ( $# -lt 2 || -z "$2" ) ]]; then
        printf 'Missing value for %s\n' "$1" >&2
        exit 2
    fi
    case "$1" in
        -r|--run-mode|--mode) RUN_MODE="$2"; shift 2 ;;
        -v|--compute-unit|--soc-version|--soc) COMPUTE_UNIT="$2"; shift 2 ;;
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
# Concrete chip names remain aliases for existing callers; compilation uses the family.
case "${COMPUTE_UNIT,,}" in
    ascend910b|ascend910b1|ascend910b2|ascend910b2c|ascend910b3|ascend910b4) COMPUTE_UNIT=ascend910b ;;
    ascend910_93|ascend910_9362|ascend910_9372|ascend910_9381|ascend910_9382|ascend910_9391|ascend910_9392)
        COMPUTE_UNIT=ascend910_93 ;;
    *) printf 'Supported compute units: ascend910b (A2), ascend910_93 (A3).\n' >&2; exit 2 ;;
esac
[[ "${JOBS}" =~ ^[1-9][0-9]*$ ]] || { usage; exit 2; }
[[ "${RUN_MODE}" == sim || "${RUN_MODE}" == npu ]] || { usage; exit 2; }
if [[ "${RUN_MODE}" == sim && "${COMPUTE_UNIT}" != ascend910b ]]; then
    printf 'Local Camodel validation uses Ascend910B1 (ascend910b); use -r npu for A3.\n' >&2
    exit 2
fi

load_cann()
{
    local candidate
    CANN_ROOT="${SDK_ARG:-${ASCEND_CANN_PACKAGE_PATH:-${ASCEND_INSTALL_PATH:-${ASCEND_HOME_PATH:-}}}}"
    if [[ -z "${CANN_ROOT}" ]]; then
        local candidates=("${HOME}/Ascend/cann-9.0.0" /usr/local/Ascend/cann-9.0.0
            "${HOME}/Ascend/ascend-toolkit/9.0.0" /usr/local/Ascend/ascend-toolkit/9.0.0)
        for candidate in "${candidates[@]}"; do
            if [[ -f "${candidate}/include/acl/acl.h" ]]; then CANN_ROOT="${candidate}"; break; fi
        done
    fi
    if [[ ! -f "${CANN_ROOT}/include/acl/acl.h" ]]; then
        printf 'Set ASCEND_HOME_PATH or ASCEND_CANN_PACKAGE_PATH to the CANN 9.0.0 SDK.\n' >&2
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
    export NTT_PYTHON="${PYTHON}"
}

set_build_paths()
{
    local tool_key
    tool_key=$({
        printf '%s\n' "${CANN_ROOT}" "${COMPUTE_UNIT}" "${RUN_MODE}" "${BUILD_TYPE}" "${PYTHON}"
        "${PYTHON}" --version
        command -v "${CXX}"
        "${CXX}" --version
        cmake --version
        "${CANN_ROOT}/tools/bisheng_compiler/bin/bisheng" --version
        sha256sum "${CANN_ROOT}/lib64/cmake/asc-config.cmake"
    } | sha256sum | cut -d ' ' -f 1)
    SOURCE_KEY=$({
        cd "${NTT_DIR}"
        find . -type d \( -name build -o -name __pycache__ -o -name CMakeFiles \) -prune -o \
            -type f ! -name README.md -print0 |
            sort -z | xargs -0 sha256sum
    } | sha256sum | cut -d ' ' -f 1)
    BUILD_DIR="${NTT_DIR}/build/asc/${tool_key:0:16}-${SOURCE_KEY:0:16}"
    if [[ "${ACCEPT_FORCE_REBUILD:-0}" == 1 ]]; then
        mkdir -p "${NTT_DIR}/build/asc"
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
          -x "${EXAMPLE}" && -f "${BUILD_DIR}/lib/libcann_crypto_ntt_device.so" ]]; then
        printf '[REUSE] Kernel-launch build matches current sources and toolchain\n'
    else
        cmake -S "${NTT_DIR}" -B "${BUILD_DIR}" \
            -DCANN_BUILD_DEVICE=ON -DRUN_MODE="${RUN_MODE}" \
            -DCANN_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
            -DASCEND_CANN_PACKAGE_PATH="${CANN_ROOT}" -DASCEND_COMPUTE_UNIT="${COMPUTE_UNIT}" \
            -DPython3_EXECUTABLE="${PYTHON}" \
            -DCMAKE_CXX_COMPILER="${CXX}" -DCMAKE_INSTALL_PREFIX="${BUILD_DIR}/install"
        cmake --build "${BUILD_DIR}" -j "${JOBS}"
        cmake --install "${BUILD_DIR}" --prefix "${BUILD_DIR}/install"
        printf '%s\n' "${SOURCE_KEY}" > "${BUILD_DIR}/.source_key"
    fi
    flock -u "${lock_fd}"
    exec {lock_fd}>&-
    printf '[BUILD] source=%s\n[BUILD] directory=%s\n' "${SOURCE_KEY}" "${BUILD_DIR}"
}

load_cann
set_build_paths
case "${STAGE}" in
    build|install) build_device ;;
    package) build_device; (cd "${BUILD_DIR}" && cpack --config CPackConfig.cmake) ;;
    env) printf '[SDK] %s\n[MODE] %s\n[COMPUTE UNIT] %s\n[ARCH] dav-2201\n[BUILD] %s\n[KERNEL LAUNCH] %s\n' "${CANN_ROOT}" "${RUN_MODE}" "${COMPUTE_UNIT}" "${BUILD_DIR}" "${EXAMPLE}" ;;
    run) build_device; bash "${SCRIPT_DIR}/run_example.sh" "${EXAMPLE}" "${CANN_ROOT}" "${RUN_MODE}" "${COMPUTE_UNIT}" ;;
esac
