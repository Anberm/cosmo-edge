#!/usr/bin/env bash
set -euo pipefail

if ! command -v docker >/dev/null 2>&1; then
    echo "ERROR: Docker is not installed or is not on PATH." >&2
    exit 1
fi

run_compose() {
    local privilege_prefix="$1"
    shift
    if ${privilege_prefix} docker compose version >/dev/null 2>&1; then
        exec ${privilege_prefix} docker compose "$@"
    fi
    if command -v docker-compose >/dev/null 2>&1 &&
       ${privilege_prefix} docker-compose version >/dev/null 2>&1; then
        exec ${privilege_prefix} docker-compose "$@"
    fi
    return 1
}

if docker info >/dev/null 2>&1; then
    run_compose "" "$@" || true
    echo "ERROR: Docker is reachable, but neither Compose V2 nor Compose V1 is installed." >&2
    exit 1
fi

if ! command -v sudo >/dev/null 2>&1; then
    echo "ERROR: Docker is not accessible to this user and sudo is unavailable." >&2
    exit 1
fi
if [ ! -t 0 ]; then
    echo "ERROR: Docker requires elevated access. Re-run interactively or grant this user Docker access." >&2
    exit 1
fi

echo "Docker requires elevated access; requesting sudo once." >&2
sudo -v
if ! sudo docker info >/dev/null 2>&1; then
    echo "ERROR: Docker is unavailable even with sudo." >&2
    exit 1
fi
run_compose "sudo" "$@" || true
echo "ERROR: neither 'sudo docker compose' nor 'sudo docker-compose' is available." >&2
exit 1
