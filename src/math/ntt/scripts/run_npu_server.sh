#!/usr/bin/env bash
# Build and run this repository's NTT through the installed CANN SDK.
set -euo pipefail
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
NTT_DIR=$(cd "${SCRIPT_DIR}/.." && pwd)
REPO_ROOT=$(cd "${NTT_DIR}/../../.." && pwd)
STAGE="${1:-plan}"
if [[ $# -gt 0 ]]; then shift; fi
SOC=ascend910b
JOBS="${NTT_BUILD_JOBS:-8}"
VENDOR="${ACCEPT_VENDOR:-cann_crypto}"
MODE=sim
case "${STAGE}" in npu-test|test) MODE=npu ;; esac

usage()
{
    printf 'Usage: bash %s [build|package|sim-test|npu-test|env|plan] [--soc ascend910b] [--jobs N] [--vendor NAME]\n' "$0"
    printf 'KEM/DSA/RNS acceptance: bash %s/{kem,dsa,rns}_entry.sh\n' "${SCRIPT_DIR}"
}
while [[ $# -gt 0 ]]; do
    case "$1" in
        --soc) SOC="$2"; shift 2 ;;
        --jobs) JOBS="$2"; shift 2 ;;
        --vendor|--vendor-name) VENDOR="$2"; shift 2 ;;
        --mode) MODE="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) printf 'Unsupported argument: %s\n' "$1" >&2; exit 2 ;;
    esac
done
case "${SOC,,}" in ascend910b|ascend910b1|ascend910b2|ascend910b3|ascend910b4) SOC=ascend910b ;;
    *) printf 'NTT currently supports ascend910b only.\n' >&2; exit 2 ;; esac
[[ "${JOBS}" =~ ^[1-9][0-9]*$ && "${VENDOR}" =~ ^[a-zA-Z0-9_]+$ ]] || { usage; exit 2; }
[[ "${MODE}" == sim || "${MODE}" == npu ]] || { usage; exit 2; }

load_cann()
{
    local candidate
    CANN_ROOT="${ASCEND_CANN_PACKAGE_PATH:-${ASCEND_HOME_PATH:-}}"
    if [[ -z "${CANN_ROOT}" ]]; then
        for candidate in "${HOME}/Ascend/ascend-toolkit/latest" /usr/local/Ascend/ascend-toolkit/latest /usr/local/Ascend/latest; do
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
    export CXX="${CXX:-c++}"
    SIM_LIB="${CANN_ROOT}/tools/simulator/Ascend910B1/lib"
}

set_build_paths()
{
    local tool_key
    tool_key=$({
        printf '%s\n' "${CANN_ROOT}" "${SOC}" "${VENDOR}" "${MODE}"
        command -v "${CXX}"
        "${CXX}" --version
        cmake --version
        "${CANN_ROOT}/tools/bisheng_compiler/bin/bisheng" --version
        sha256sum "${CANN_ROOT}/compiler/tikcpp/ascendc_kernel_cmake/fwk_modules/func.cmake"
    } | sha256sum | cut -d ' ' -f 1)
    BUILD_DIR="${REPO_ROOT}/build/ntt_device/${tool_key:0:16}"
    OPP_DIR="${BUILD_DIR}/opp/packages/vendors/${VENDOR}"
    EXAMPLE="${BUILD_DIR}/bin/ntt_aclnn_example"
    SOURCE_KEY=$({
        sha256sum "${REPO_ROOT}/CMakeLists.txt"
        find "${NTT_DIR}" -type f ! -name README.md ! -name LICENSE ! -path '*/__pycache__/*' -print0 |
            sort -z | xargs -0 sha256sum
    } | sha256sum | cut -d ' ' -f 1)
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
          -x "${EXAMPLE}" && -f "${OPP_DIR}/op_api/lib/libcust_opapi.so" &&
          -f "${OPP_DIR}/op_impl/ai_core/tbe/kernel/config/${SOC}/binary_info_config.json" ]]; then
        printf '[REUSE] CANN SDK build matches current sources and toolchain\n'
    else
        local simulate=OFF
        [[ "${MODE}" != sim ]] || simulate=ON
        cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" \
            -DCRYPTO_NTT_DEVICE=ON -DCRYPTO_NTT_SIMULATOR="${simulate}" \
            -DCRYPTO_NTT_CANN_ADAPTER=OFF -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release \
            -DASCEND_CANN_PACKAGE_PATH="${CANN_ROOT}" -DASCEND_COMPUTE_UNIT="${SOC}" \
            -DCRYPTO_NTT_VENDOR="${VENDOR}" -DCMAKE_CXX_COMPILER="${CXX}"
        # SDK copy/codegen targets must also refresh when a kernel header changes.
        cmake --build "${BUILD_DIR}" --clean-first -j "${JOBS}"
        cmake --install "${BUILD_DIR}" --prefix "${BUILD_DIR}/opp"
        printf '%s\n' "${SOURCE_KEY}" > "${BUILD_DIR}/.source_key"
    fi
    flock -u "${lock_fd}"
    exec {lock_fd}>&-
    printf '[BUILD] source=%s\n[BUILD] directory=%s\n' "${SOURCE_KEY}" "${BUILD_DIR}"
}

activate_package()
{
    export ASCEND_CUSTOM_OPP_PATH="${OPP_DIR}"
    export LD_LIBRARY_PATH="${OPP_DIR}/op_api/lib:${CANN_ROOT}/lib64:${LD_LIBRARY_PATH:-}"
    if [[ "${MODE}" == sim ]]; then
        export LD_LIBRARY_PATH="${BUILD_DIR}/simulator_compat:${SIM_LIB}:${LD_LIBRARY_PATH}"
    fi
    export ASCEND_SLOG_PRINT_TO_STDOUT="${ASCEND_SLOG_PRINT_TO_STDOUT:-0}"
    export ASCEND_GLOBAL_LOG_LEVEL="${ASCEND_GLOBAL_LOG_LEVEL:-3}"
    export ASCEND_GLOBAL_EVENT_ENABLE="${ASCEND_GLOBAL_EVENT_ENABLE:-0}"
    printf '[OPP] %s\n' "${OPP_DIR}"
}

run_example()
{
    build_device
    activate_package
    local run_dir="${NTT_RUN_DIR:-}"
    if [[ -z "${run_dir}" ]]; then
        mkdir -p "${REPO_ROOT}/build/ntt_acceptance"
        run_dir=$(mktemp -d "${REPO_ROOT}/build/ntt_acceptance/${NTT_PROFILE:-rns}.XXXXXX")
    fi
    mkdir -p "${run_dir}/camodel" "${run_dir}/plog"
    run_dir=$(cd "${run_dir}" && pwd)
    export CAMODEL_LOG_PATH="${CAMODEL_LOG_PATH:-${run_dir}/camodel}"
    export ASCEND_PROCESS_LOG_PATH="${ASCEND_PROCESS_LOG_PATH:-${run_dir}/plog}"
    export CAMODEL_CONFIG_PATH="${CAMODEL_CONFIG_PATH:-${SCRIPT_DIR}/camodel_config}"
    export NTT_TRACE_TILING="${NTT_TRACE_TILING:-1}"
    local default_bench=1
    case "${NTT_PROFILE:-rns}" in ml-kem|ml-dsa) default_bench=4096 ;; esac
    export NTT_BENCH="${NTT_BENCH:-${default_bench}}"
    cp "${CAMODEL_CONFIG_PATH}/config.json" "${run_dir}/camodel_config.json"
    printf '[RUN] directory=%s\n[RUN] model_config=%s\n' "${run_dir}" "${CAMODEL_CONFIG_PATH}"
    (cd "${run_dir}" && "${EXAMPLE}" 2>&1 | tee run.log)
}

case "${STAGE}" in
    plan|-h|--help) usage ;;
    kem-entry) exec bash "${SCRIPT_DIR}/kem_entry.sh" ;;
    dsa-entry) exec bash "${SCRIPT_DIR}/dsa_entry.sh" ;;
    rns-entry) exec bash "${SCRIPT_DIR}/rns_entry.sh" ;;
    build|install|package|sim-test|npu-test|test|env)
        load_cann
        set_build_paths
        case "${STAGE}" in
            build|install) build_device ;;
            package) build_device; cmake --build "${BUILD_DIR}" --target package -j "${JOBS}" ;;
            env) printf '[SDK] %s\n[BUILD] %s\n[OPP] %s\n' "${CANN_ROOT}" "${BUILD_DIR}" "${OPP_DIR}" ;;
            *) run_example ;;
        esac
        ;;
    *) usage; exit 2 ;;
esac
