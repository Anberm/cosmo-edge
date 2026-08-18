#!/bin/bash
set -euo pipefail

# Keep one authoritative Sophon build path. A clean test build uses the same
# configure-time Guard SDK admission and packaging profile as the final build.
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd -P)"
export COSMO_MODEL_GUARD_BUILD_PROFILE="${COSMO_MODEL_GUARD_BUILD_PROFILE:-public-runtime}"
exec "${SCRIPT_DIR}/build.sh" -T "$@"
