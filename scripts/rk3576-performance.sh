#!/bin/sh
set -eu

set_governor() {
    path="$1"
    governor="$2"
    if [ -w "$path" ]; then
        printf '%s\n' "$governor" > "$path"
    fi
}

set_governor /sys/class/devfreq/27700000.npu/governor performance
set_governor /sys/class/devfreq/dmc/governor performance

for policy in /sys/devices/system/cpu/cpufreq/policy*; do
    set_governor "$policy/scaling_governor" performance
done
