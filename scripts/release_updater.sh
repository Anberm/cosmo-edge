#!/bin/bash
set -eu
IFS=$' \t\n'
PATH='/usr/sbin:/usr/bin:/sbin:/bin'
export IFS PATH

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd -P)"
implementation="${SCRIPT_DIR}/release_updater.py"

if [ ! -f "$implementation" ]; then
    echo "Signed release updater implementation is unavailable" >&2
    exit 1
fi

exec /usr/bin/python3 -I -B "$implementation" "$@"
