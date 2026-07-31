#!/bin/bash
set -euo pipefail

# Keep one authoritative protected-build path.  A clean Sophon test build needs
# the same dependency-producer bootstrap, SDK admission, install audit, and
# package gate as the selected build profile.
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd -P)"
export COSMO_MODEL_GUARD_BUILD_PROFILE="${COSMO_MODEL_GUARD_BUILD_PROFILE:-public-runtime}"
exec "${SCRIPT_DIR}/build.sh" -T "$@"
