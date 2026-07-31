#!/bin/bash
set -eu
IFS=$' \t\n'
PATH='/usr/sbin:/usr/bin:/sbin:/bin'
export IFS PATH

# Trusted release-transaction wrapper.  This file is always executed from the
# currently active release; an incoming archive's install script is never run.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd -P)"

# shellcheck source=common.sh
. "${SCRIPT_DIR}/common.sh"

updater="${SCRIPT_DIR}/release_updater.sh"
if [ ! -f "$updater" ]; then
    echo "[INSTALL] release updater is unavailable" >&2
    exit 1
fi

action="${1:-}"
case "$action" in
    prepare)
        if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
            echo "Usage: $0 prepare <signed-release.tar.gz> [logfile]" >&2
            exit 2
        fi
        archive="$2"
        log_file="${3:-/dev/null}"
        cosmo_log "INSTALL" "Validating and staging signed compatibility release" "$log_file"
        "$updater" prepare "$archive"
        cosmo_log "INSTALL" "Signed release staged; active release is unchanged" "$log_file"
        ;;
    run-pending-health)
        if [ "$#" -ne 3 ]; then
            echo "Usage: $0 run-pending-health <runner-pid> <expected-release-directory>" >&2
            exit 2
        fi
        "$updater" "$action" "$2" "$3"
        ;;
    activate|commit-healthy|rollback|recover|active-path|pending-path|pending-health-script)
        if [ "$#" -ne 1 ]; then
            echo "Usage: $0 ${action}" >&2
            exit 2
        fi
        "$updater" "$action"
        ;;
    *)
        echo "Usage: $0 {prepare <archive> [logfile]|activate|commit-healthy|rollback|recover|active-path|pending-path|pending-health-script|run-pending-health <runner-pid> <expected-release-directory>}" >&2
        exit 2
        ;;
esac
