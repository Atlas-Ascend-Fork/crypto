#!/usr/bin/env bash

set -euo pipefail
ENTRY_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=acceptance_common.sh
source "${ENTRY_DIR}/acceptance_common.sh"
run_pqc_acceptance "ml-kem" "3329" \
    "ML-KEM-512,ML-KEM-768,ML-KEM-1024"
