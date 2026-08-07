#!/bin/bash
set -eu

# Expose only the vendor's read-only NPU load counter outside debugfs. This
# lets an unprivileged cosmo-engine read it without access to the rest of debugfs.
SOURCE_PATH="/sys/kernel/debug/rknpu/load"
TARGET_DIR="/run/cosmo-edge/metrics"
TARGET_PATH="${TARGET_DIR}/rknpu-load"

if [ ! -r "${SOURCE_PATH}" ]; then
    exit 0
fi

if [ "$(id -u)" -ne 0 ]; then
    echo "RKNN metrics bridge skipped: root is required" >&2
    exit 0
fi

install -d -o root -g root -m 0755 "${TARGET_DIR}"

mounted_target="$(findmnt -rn -o TARGET --target "${TARGET_PATH}" 2>/dev/null || true)"
if [ "${mounted_target}" = "${TARGET_PATH}" ]; then
    exit 0
fi

install -o root -g root -m 0444 /dev/null "${TARGET_PATH}"
if ! mount --bind "${SOURCE_PATH}" "${TARGET_PATH}"; then
    rm -f "${TARGET_PATH}"
    echo "RKNN metrics bridge failed: cannot bind ${SOURCE_PATH}" >&2
    exit 1
fi

if ! mount -o remount,bind,ro "${TARGET_PATH}"; then
    umount "${TARGET_PATH}" || true
    rm -f "${TARGET_PATH}"
    echo "RKNN metrics bridge failed: cannot make target read-only" >&2
    exit 1
fi

echo "RKNN metrics bridge ready: ${TARGET_PATH}"
