#!/bin/bash

# Stop managed processes gracefully (SIGTERM first, then SIGKILL)
PROC_LIST="cosmo-engine srs nginx"

for proc in $PROC_LIST; do
    pids=$(pidof "$proc" 2>/dev/null) || true
    if [ -n "$pids" ]; then
        echo "Stopping $proc (PID: $pids)..."
        kill -15 $pids 2>/dev/null || true
    fi
done

# Give cosmo-engine enough time to leave its HTTP loop, disable the hardware
# watchdog, and drain managed workers before falling back to SIGKILL.
graceful_timeout="${COSMO_STOP_TIMEOUT_SECONDS:-15}"
case "$graceful_timeout" in
    ''|*[!0-9]*) graceful_timeout=15 ;;
esac

elapsed=0
while [ "$elapsed" -lt "$graceful_timeout" ]; do
    any_running=0
    for proc in $PROC_LIST; do
        if pidof "$proc" >/dev/null 2>&1; then
            any_running=1
            break
        fi
    done
    if [ "$any_running" -eq 0 ]; then
        break
    fi
    sleep 1
    elapsed=$((elapsed + 1))
done

# Force kill any remaining processes
for proc in $PROC_LIST; do
    pids=$(pidof "$proc" 2>/dev/null) || true
    if [ -n "$pids" ]; then
        echo "Graceful shutdown timeout; force killing $proc (PID: $pids)..."
        kill -9 $pids 2>/dev/null || true
    fi
done
