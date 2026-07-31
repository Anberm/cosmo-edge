#!/bin/bash
set -eu

IFS=$' \t\n'
PATH='/usr/sbin:/usr/bin:/sbin:/bin'
export IFS PATH

readonly RUNTIME_ROOT='/appfs/cosmo_wander/cwai_data'
readonly HTTP_PORT=8000

fail() {
    echo "[SOURCE-HEALTH] ERROR: $*" >&2
    exit 1
}

[ "$#" -eq 0 ] || fail "this health check accepts no arguments"
[ -x "${RUNTIME_ROOT}/bin/cosmo-engine" ] ||
    fail "cosmo-engine is missing"
[ -f "${RUNTIME_ROOT}/lib/libcosmo_model_guard.so.2.0.0" ] ||
    fail "Model Guard library is missing"
pidof cosmo-engine >/dev/null 2>&1 ||
    fail "cosmo-engine is not running"

[ -x /usr/bin/python3 ] || fail "python3 is unavailable"
/usr/bin/python3 -I -B -c '
import socket

for family, address in (
    (socket.AF_INET, ("127.0.0.1", 8000)),
    (socket.AF_INET6, ("::1", 8000, 0, 0)),
):
    try:
        with socket.socket(family, socket.SOCK_STREAM) as connection:
            connection.settimeout(1.0)
            connection.connect(address)
            connection.sendall(
                b"HEAD / HTTP/1.0\r\nHost: localhost\r\nConnection: close\r\n\r\n"
            )
            if connection.recv(64).startswith(b"HTTP/"):
                raise SystemExit(0)
    except OSError:
        pass
raise SystemExit(1)
' </dev/null >/dev/null 2>&1 ||
    fail "HTTP endpoint on port 8000 did not respond"
