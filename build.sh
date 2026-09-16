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
# 仓库构建与测试入口，同时供开发者本地使用和 CI 门禁调用。

set -e
set -o pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_OUT_PATH="${SCRIPT_DIR}/build_out"

ENABLE_PACKAGE="FALSE"
ENABLE_TEST="FALSE"
UT_LAYERS=""
UT_TIMEOUT="120"
ALLOW_EMPTY="FALSE"
THREAD_NUM="$(nproc)"
COMPONENTS=""

dotted_line="------------------------------------------------------------"

usage() {
  cat <<'USAGE'
用法: bash build.sh [选项]

构建选项:
    --pkg                  构建全部组件
    --components=a,b       只构建指定组件，逗号分隔，如 --components=primitives/AES
    -j[n]                  编译并发数，默认为 CPU 核数

测试选项:
    -u                     运行单元测试，未指定层时运行全部
    --ophost               仅运行 op_host 层用例，无需昇腾设备。CMake 组件以
                           Host 配置构建后，执行未打 device、kernel_ut 标签的用例
    --opapi                仅运行 op_api 层用例，无需昇腾设备
    --opgraph              仅运行 op_graph 层用例，无需昇腾设备
    --opkernel             仅运行 op_kernel 层用例，需要昇腾设备与 CANN 环境。
                           未设置 ASCEND_HOME_PATH 时跳过设备用例
    --ut_timeout=<N>       单用例超时秒数，默认 120
    --allow_empty          指定层次下未发现用例时仅告警并返回成功，默认报错

其他:
    -h, --help             显示本帮助

环境变量:
    ASCEND_HOME_PATH       CANN 安装路径。已设置时构建设备算子包并可运行设备用例，
                           未设置时仅构建 Host 侧，便于无卡环境开发自检
    ASCEND_COMPUTE_UNIT    设备编译目标，默认 ascend910b（Atlas A2），
                           Atlas A3 取 ascend910_93

示例:
    bash build.sh --pkg                 # 编译全部组件
    bash build.sh -u --ophost           # 门禁：只跑 op_host 层，无需设备
    bash build.sh -u --opkernel         # 冒烟：需要昇腾设备
    bash build.sh --pkg -u              # 编译并运行全部用例
    bash build.sh --components=primitives/AES --pkg
USAGE
}

log() { echo "[$(date '+%Y-%m-%d %H:%M:%S')] $*"; }

checkopts() {
  while [ -n "$1" ]; do
    case "$1" in
      --pkg)            ENABLE_PACKAGE="TRUE" ;;
      -u)               ENABLE_TEST="TRUE" ;;
      --ophost)         UT_LAYERS="${UT_LAYERS} op_host" ;;
      --opapi)          UT_LAYERS="${UT_LAYERS} op_api" ;;
      --opgraph)        UT_LAYERS="${UT_LAYERS} op_graph" ;;
      --opkernel)       UT_LAYERS="${UT_LAYERS} op_kernel" ;;
      --ut_timeout=*)   UT_TIMEOUT="${1#*=}" ;;
      --allow_empty)    ALLOW_EMPTY="TRUE" ;;
      --components=*)   COMPONENTS="${1#*=}" ;;
      -j*)              THREAD_NUM="${1#-j}" ;;
      -h|--help)        usage; exit 0 ;;
      *)                echo "未知选项: $1"; usage; exit 1 ;;
    esac
    shift
  done
  if [ "${ENABLE_PACKAGE}" == "FALSE" ] && [ "${ENABLE_TEST}" == "FALSE" ]; then
    usage; exit 1
  fi
}

# 列出待处理组件。组件为 src 下自带构建文件、且其祖先目录均无构建文件的目录，
# 因此层级不限：primitives/AES、primitives/keccak、math/ntt 为两级，
# algorithms/hash/shake 为三级，而 tests、examples、cmake 等子目录不会被误判为组件。
list_components() {
  if [ -n "${COMPONENTS}" ]; then
    echo "${COMPONENTS}" | tr ',' '\n'
    return
  fi
  local file dir rel parent is_nested
  while IFS= read -r file; do
    dir="$(dirname "${file}")"
    rel="${dir#"${SCRIPT_DIR}/src/"}"
    is_nested=0
    parent="$(dirname "${dir}")"
    while [ "${parent}" != "${SCRIPT_DIR}/src" ] && [ "${parent}" != "/" ]; do
      if [ -f "${parent}/CMakeLists.txt" ] || [ -f "${parent}/setup.py" ]; then
        is_nested=1
        break
      fi
      parent="$(dirname "${parent}")"
    done
    [ "${is_nested}" -eq 0 ] && echo "${rel}"
  done < <(find "${SCRIPT_DIR}/src" \( -name CMakeLists.txt -o -name setup.py \) -type f 2>/dev/null | sort) | sort -u
}

# CANN 算子框架型组件（keccak、shake）的构建开关。
# 有 CANN 环境时构建设备算子包，否则只构建 Host 侧，便于本地开发与无卡环境自检。
cann_cmake_args() {
  local build_tests="$1"
  local args=(-DCMAKE_BUILD_TYPE=Release "-DCANN_BUILD_TESTS=${build_tests}" -DCANN_BUILD_CPU_DEBUG_TESTS=OFF)
  if [ -n "${ASCEND_HOME_PATH}" ]; then
    args+=(-DCANN_BUILD_DEVICE=ON "-DASCEND_COMPUTE_UNIT=${ASCEND_COMPUTE_UNIT:-ascend910b}"
           "-DASCEND_CANN_PACKAGE_PATH=${ASCEND_HOME_PATH}"
           "-DPython3_EXECUTABLE=$(command -v python3)" "-DCANN_BUILD_JOBS=${THREAD_NUM}")
  else
    args+=(-DCANN_BUILD_DEVICE=OFF)
  fi
  printf '%s\n' "${args[@]}"
}

# 组件是否使用 CANN 算子框架，以顶层 CMakeLists.txt 中的 CANN_BUILD_TESTS 开关判定。
is_cann_component() {
  [ -f "$1/CMakeLists.txt" ] && grep -q 'CANN_BUILD_TESTS' "$1/CMakeLists.txt"
}

build_component() {
  local name="$1" dir="${SCRIPT_DIR}/src/$1"
  # setup.py 优先：部分组件由 setup.py 驱动 CMake 并注入依赖路径，
  # 直接对其 CMakeLists.txt 执行 cmake 会缺少这些路径。
  if [ -f "${dir}/setup.py" ]; then
    log "构建 ${name}（Python wheel）"
    ( cd "${dir}"
      [ -f requirements.txt ] && pip install -r requirements.txt
      python3 -m build --wheel --no-isolation
      python3 -m pip install dist/*.whl --force-reinstall --no-deps )
  elif is_cann_component "${dir}"; then
    local args=()
    mapfile -t args < <(cann_cmake_args OFF)
    if [ -n "${ASCEND_HOME_PATH}" ]; then
      log "构建 ${name}（CANN 算子包，${ASCEND_COMPUTE_UNIT:-ascend910b}）"
    else
      log "构建 ${name}（CMake，未检测到 CANN，仅构建 Host 侧）"
    fi
    cmake -S "${dir}" -B "${dir}/build" "${args[@]}"
    cmake --build "${dir}/build" -j "${THREAD_NUM}"
  elif [ -f "${dir}/CMakeLists.txt" ]; then
    log "构建 ${name}（CMake）"
    cmake -S "${dir}" -B "${dir}/build" -DCMAKE_BUILD_TYPE=Release
    cmake --build "${dir}/build" -j "${THREAD_NUM}"
  else
    log "跳过 ${name}：未找到构建文件"
  fi
}

has_tests() {
  [ -d "$1" ] && [ -n "$(find "$1" \( -name 'test_*' -o -name '*_test.*' \) 2>/dev/null | head -1)" ]
}

build_package() {
  echo ${dotted_line}; log "开始编译"
  mkdir -p "${BUILD_OUT_PATH}"
  local built=0
  for c in $(list_components); do
    build_component "${c}"
    built=$((built + 1))
  done
  [ "${built}" -eq 0 ] && log "未发现可构建组件"
  log "编译完成，共 ${built} 个组件"
}

build_ut() {
  # 探测 pytest-timeout 是否可用，决定是否传 --timeout
  PYTEST_TIMEOUT_ARG=""
  if python3 -c "import pytest_timeout" >/dev/null 2>&1; then
    PYTEST_TIMEOUT_ARG="--timeout=${UT_TIMEOUT}"
  else
    log "未安装 pytest-timeout，pytest 用例不设置超时"
  fi

  local layers="${UT_LAYERS}"
  [ -z "${layers}" ] && layers="common op_api op_graph op_host op_kernel"
  echo ${dotted_line}; log "开始测试，层次：${layers}"

  local ran=0 failed=()

  # 只执行一次 pytest。超时参数依赖 pytest-timeout，事先探测其是否可用，
  # 不能靠「失败后去掉参数重跑」，那样会把真实的用例失败掩盖成通过。
  run_pytest() {
    local dir="$1" label="$2"
    has_tests "${dir}" || return 0
    ran=$((ran + 1))
    log "测试 ${label}（pytest）"
    pytest "${dir}" -v ${PYTEST_TIMEOUT_ARG} || failed+=("${label}")
  }

  # 配置并构建组件，随后按标签筛选用例执行。
  # ctest 必须对构建目录执行：对源码目录执行时没有 CTestTestfile.cmake，
  # ctest 会打印 No tests were found 并返回 0，门禁将在零覆盖下亮绿灯。
  run_ctest() {
    local comp="$1" label="$2" build_dir="$3" select="$4"; shift 4
    local component_dir="${SCRIPT_DIR}/src/${comp}"
    ran=$((ran + 1))
    log "测试 ${label}（CTest）"
    if ! (
      cmake -S "${component_dir}" -B "${build_dir}" "$@" &&
      cmake --build "${build_dir}" -j "${THREAD_NUM}" &&
      # shellcheck disable=SC2086
      ctest --test-dir "${build_dir}" ${select} --output-on-failure \
        --no-tests=error --timeout "${UT_TIMEOUT}"
    ); then
      failed+=("${label}")
    fi
  }

  # 仓库级用例：tests/ut/<layer>
  for l in ${layers}; do
    run_pytest "${SCRIPT_DIR}/tests/ut/${l}" "tests/ut/${l}"
  done

  local soc="${ASCEND_COMPUTE_UNIT:-ascend910b}"
  for c in $(list_components); do
    local comp_dir="${SCRIPT_DIR}/src/${c}" base="${SCRIPT_DIR}/src/${c}/tests"

    # op_host 层：不依赖昇腾设备，构建 Host 侧后执行未打 device、kernel_ut 标签的用例。
    # 以 tests/CMakeLists.txt 为准：仅 CTest 组织的测试走此分支，
    # 测试为 pytest 的组件（如 AES）不在此层，其用例需要设备，归入 op_kernel。
    if echo "${layers}" | grep -qw op_host && [ -f "${base}/CMakeLists.txt" ] \
        && [ -f "${comp_dir}/CMakeLists.txt" ]; then
      if is_cann_component "${comp_dir}"; then
        local host_args=()
        mapfile -t host_args < <(cann_cmake_args ON)
        # Host 用例不依赖设备，固定关闭设备构建，避免无 CANN 环境时失败
        host_args=("${host_args[@]/-DCANN_BUILD_DEVICE=ON/-DCANN_BUILD_DEVICE=OFF}")
        run_ctest "${c}" "${c}/op_host" "${comp_dir}/build/host" \
          "-LE device|kernel_ut" "${host_args[@]}"
      else
        run_ctest "${c}" "${c}/op_host" "${comp_dir}/build/host" \
          "-LE device|kernel_ut" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
      fi
    fi

    # op_kernel 层：需要昇腾设备
    if echo "${layers}" | grep -qw op_kernel; then
      if is_cann_component "${comp_dir}"; then
        if [ -z "${ASCEND_HOME_PATH}" ]; then
          log "跳过 ${c}/op_kernel：未设置 ASCEND_HOME_PATH，设备用例需要 CANN 环境"
        else
          local dev_args=()
          mapfile -t dev_args < <(cann_cmake_args ON)
          run_ctest "${c}" "${c}/op_kernel" "${comp_dir}/build/device-${soc}" \
            "-L device" "${dev_args[@]}"
        fi
      elif [ -f "${comp_dir}/scripts/run_npu_server.sh" ]; then
        if [ -z "${ASCEND_HOME_PATH}" ]; then
          log "跳过 ${c}/op_kernel：未设置 ASCEND_HOME_PATH，设备用例需要 CANN 环境"
        else
          # ntt 的设备验证由组件自带脚本完成：构建设备包并与标准答案比对
          ran=$((ran + 1))
          log "测试 ${c}/op_kernel（run_npu_server.sh npu-test）"
          bash "${comp_dir}/scripts/run_npu_server.sh" npu-test --soc "${soc}" \
            --jobs "${THREAD_NUM}" || failed+=("${c}/op_kernel")
        fi
      fi
      # 未按层拆分的组件（如 AES），其 tests 目录整体归入 op_kernel
      [ -d "${base}/ut" ] || run_pytest "${base}" "${c}"
    fi

    # 其余层次沿用目录约定：src/<组件>/tests/ut/<layer>
    for l in ${layers}; do
      [ "${l}" = "op_host" ] && continue
      [ "${l}" = "op_kernel" ] && continue
      run_pytest "${base}/ut/${l}" "${c}/ut/${l}"
    done
  done

  if [ "${ran}" -eq 0 ]; then
    if [ -n "${UT_LAYERS}" ]; then
      # 显式指定了层次却一个用例都没发现，说明用例缺失或路径不对。
      # 此处若返回成功，门禁会在零覆盖的情况下亮绿灯。
      if [ "${ALLOW_EMPTY}" == "TRUE" ]; then
        log "警告：指定层次 ${UT_LAYERS} 下未发现任何用例，已指定 --allow_empty，按通过处理"
        return 0
      fi
      log "错误：指定层次 ${UT_LAYERS} 下未发现任何用例"
      return 1
    fi
    log "未发现可执行的用例"
    return 0
  fi
  if [ ${#failed[@]} -gt 0 ]; then
    log "失败: ${failed[*]}"
    return 1
  fi
  log "全部通过，共 ${ran} 组"
}

main() {
  checkopts "$@"
  [ "${ENABLE_PACKAGE}" == "TRUE" ] && build_package
  [ "${ENABLE_TEST}" == "TRUE" ] && build_ut
  return 0
}

main "$@"
