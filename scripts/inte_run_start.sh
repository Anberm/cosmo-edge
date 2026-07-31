#!/bin/bash
set -e

# System boot entry script - called by systemd cosmo.service to start services
# Relies on systemd After=network-online.target for network readiness.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# shellcheck source=common.sh
. "${SCRIPT_DIR}/common.sh"

cosmo_log "BOOT" "Preparing startup environment..."
ensure_runtime_dirs

cosmo_log "BOOT" "Starting Cosmo services..."

# Keep the historical systemd ExecStart path stable.  Once factory bootstrap
# has created the atomic release pointer, dispatch into that exact active
# release; the compatibility transaction never rewrites the systemd unit.
if [ -L "$COSMO_RELEASE_CURRENT" ] && [ -x "$COSMO_RELEASE_CURRENT/scripts/start.sh" ]; then
    cd "$COSMO_RELEASE_CURRENT/scripts" || exit 1
    exec "$COSMO_RELEASE_CURRENT/scripts/start.sh" start
fi

cd "$SCRIPT_DIR" || exit 1
exec "$SCRIPT_DIR/start.sh" start
