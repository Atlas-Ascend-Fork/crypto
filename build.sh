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
    --ophost               仅运行 op_host 层用例，无需昇腾设备
    --opapi                仅运行 op_api 层用例，无需昇腾设备
    --opgraph              仅运行 op_graph 层用例，无需昇腾设备
    --opkernel             仅运行 op_kernel 层用例，需要昇腾设备
    --ut_timeout=<N>       单用例超时秒数，默认 120

其他:
    -h, --help             显示本帮助

示例:
    bash build.sh --pkg                 # 编译全部组件
    bash build.sh -u --ophost           # 门禁：只跑 op_host 层，无需设备
    bash build.sh -u --opkernel         # 每日构建：需要昇腾设备
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

# 列出待处理组件。组件为 src 下的二级目录，需自带构建文件。
list_components() {
  if [ -n "${COMPONENTS}" ]; then
    echo "${COMPONENTS}" | tr ',' '\n'
    return
  fi
  for dir in "${SCRIPT_DIR}"/src/*/*/; do
    [ -d "${dir}" ] || continue
    if [ -f "${dir}/setup.py" ] || [ -f "${dir}/CMakeLists.txt" ]; then
      local n="${dir#"${SCRIPT_DIR}/src/"}"
      echo "${n%/}"
    fi
  done
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

  run_layer() {
    local dir="$1" label="$2"
    has_tests "${dir}" || return 0
    ran=$((ran + 1))
    log "测试 ${label}"
    if [ -f "${dir}/CMakeLists.txt" ]; then
      ctest --test-dir "${dir}" --output-on-failure --timeout "${UT_TIMEOUT}" \
        || failed+=("${label}")
    else
      # 只执行一次 pytest。超时参数依赖 pytest-timeout，事先探测其是否可用，
      # 不能靠「失败后去掉参数重跑」，那样会把真实的用例失败掩盖成通过。
      pytest "${dir}" -v ${PYTEST_TIMEOUT_ARG} || failed+=("${label}")
    fi
  }

  # 仓库级用例：tests/ut/<layer>
  for l in ${layers}; do
    run_layer "${SCRIPT_DIR}/tests/ut/${l}" "tests/ut/${l}"
  done

  # 组件自带用例：src/<组件>/tests/ut/<layer>
  for c in $(list_components); do
    local base="${SCRIPT_DIR}/src/${c}/tests"
    for l in ${layers}; do
      run_layer "${base}/ut/${l}" "${c}/ut/${l}"
    done
    # 未按层拆分的组件，其 tests 目录归入 op_kernel（通常需要设备）
    if [ ! -d "${base}/ut" ] && echo "${layers}" | grep -qw op_kernel; then
      run_layer "${base}" "${c}"
    fi
  done

  if [ "${ran}" -eq 0 ]; then
    if [ -n "${UT_LAYERS}" ]; then
      # 显式指定了层次却一个用例都没发现，说明用例缺失或路径不对。
      # 此处若返回成功，门禁会在零覆盖的情况下亮绿灯。
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
