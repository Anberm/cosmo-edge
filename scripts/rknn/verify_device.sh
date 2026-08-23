#!/usr/bin/env bash

set -euo pipefail

if [[ "$(uname -m)" != "aarch64" ]]; then
    echo "Expected aarch64 target, got $(uname -m)" >&2
    exit 1
fi

echo "kernel=$(uname -r)"
echo "model=$(tr -d '\0' </proc/device-tree/model 2>/dev/null || echo unknown)"

driver_line="$(dmesg 2>/dev/null | grep -i 'Initialized rknpu' | tail -1 || true)"
echo "driver=${driver_line:-unavailable}"

if [[ -r /usr/lib/librknnrt.so ]]; then
    system_runtime="$(strings /usr/lib/librknnrt.so | grep -m1 'librknnrt version' || true)"
    echo "system_runtime=${system_runtime:-unknown}"
    echo "system_runtime_sha256=$(sha256sum /usr/lib/librknnrt.so | awk '{print $1}')"
else
    echo "system_runtime=missing"
fi

for load_path in /sys/kernel/debug/rknpu/load /sys/kernel/debug/rknpu/load_balance; do
    if [[ -r "$load_path" ]]; then
        echo "npu_load[$load_path]=$(tr '\n' ' ' <"$load_path")"
    fi
done

for freq_path in /sys/class/devfreq/*npu*/cur_freq /sys/class/devfreq/*npu*/max_freq; do
    if [[ -r "$freq_path" ]]; then
        echo "$(basename "$freq_path")[$freq_path]=$(cat "$freq_path")"
    fi
done
