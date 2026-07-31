#!/bin/bash
set -euo pipefail

IFS=$' \t\n'
PATH='/usr/sbin:/usr/bin:/sbin:/bin'
export IFS PATH

# SOURCE packages deliberately bypass `start.sh`, `current`, and the signed
# release updater. They run only the application tree selected by the local
# root operator through install-device.sh.
readonly SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd -P)"
readonly INSTALL_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd -P)"

# shellcheck source=common.sh
. "${SCRIPT_DIR}/common.sh"

ensure_runtime_dirs
mkdir -p "$COSMO_LOG_DIR"
readonly LOG_FILE="${COSMO_LOG_DIR}/SOURCE_RUN.log"

cosmo_log "SOURCE_BOOT" "Starting directly from ${INSTALL_ROOT}" "$LOG_FILE"
INSTALLPATH="$INSTALL_ROOT" \
COSMO_TRUSTED_STOP_SCRIPT="${SCRIPT_DIR}/stop.sh" \
    exec "${SCRIPT_DIR}/run_start.sh" start "$LOG_FILE"
