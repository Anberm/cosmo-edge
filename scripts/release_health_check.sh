#!/bin/bash
set -eu
IFS=$' \t\n'
PATH='/usr/sbin:/usr/bin:/sbin:/bin'
export IFS PATH

# Validate the exact activated engine/guard pair before the updater commits its
# durable release generation.  This script intentionally has no environment or
# command-line override for the install root, timeout, port, or guard SONAME.

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <run-start-pid> <expected-release-directory>" >&2
    exit 2
fi

runner_pid="$1"
expected_release="$2"
install_root="/appfs/cosmo_wander/cwai_data"
expected_prefix="${install_root}/.releases/"
guard_soname="libcosmo_model_guard.so.2"
timeout_seconds=60
http_port=8000
http_port_hex="$(printf '%04X' "$http_port")"
required_stable_samples=3

engine_owns_http_listener() {
    checked_pid="$1"
    for descriptor in "/proc/${checked_pid}/fd/"*; do
        [ -L "$descriptor" ] || continue
        descriptor_target="$(readlink -- "$descriptor" 2>/dev/null || true)"
        case "$descriptor_target" in
            socket:\[*\])
                socket_inode="${descriptor_target#socket:[}"
                socket_inode="${socket_inode%]}"
                ;;
            *)
                continue
                ;;
        esac
        for socket_table in /proc/net/tcp /proc/net/tcp6; do
            [ -r "$socket_table" ] || continue
            if awk -v inode="$socket_inode" -v port="$http_port_hex" '
                NR > 1 && toupper($2) ~ (":" port "$") && $4 == "0A" && $10 == inode {
                    found = 1
                }
                END { exit(found ? 0 : 1) }
            ' "$socket_table"; then
                return 0
            fi
        done
    done
    return 1
}

engine_http_responds() {
    /usr/bin/python3 -I -B -c '
import socket

request = b"HEAD / HTTP/1.0\r\nHost: localhost\r\nConnection: close\r\n\r\n"
addresses = (
    (socket.AF_INET, ("127.0.0.1", 8000)),
    (socket.AF_INET6, ("::1", 8000, 0, 0)),
)
for family, address in addresses:
    try:
        with socket.socket(family, socket.SOCK_STREAM) as connection:
            connection.settimeout(1.0)
            connection.connect(address)
            connection.sendall(request)
            response = connection.recv(64)
            if response.startswith(b"HTTP/"):
                raise SystemExit(0)
    except OSError:
        pass
raise SystemExit(1)
' </dev/null >/dev/null 2>&1
}

runner_is_live() {
    checked_pid="$1"
    kill -0 "$checked_pid" 2>/dev/null || return 1
    [ -r "/proc/${checked_pid}/stat" ] || return 1
    process_state="$(awk '{ print $3 }' "/proc/${checked_pid}/stat" 2>/dev/null || true)"
    case "$process_state" in
        ''|Z|X) return 1 ;;
        *) return 0 ;;
    esac
}

case "$runner_pid" in
    ''|*[!0-9]*)
        echo "Release health check received an invalid runner PID" >&2
        exit 1
        ;;
esac

case "$expected_release" in
    "${expected_prefix}"[a-z0-9]*) ;;
    *)
        echo "Release health check rejected the candidate path" >&2
        exit 1
        ;;
esac

case "${expected_release#"${expected_prefix}"}" in
    *[!a-z0-9._-]*|'')
        echo "Release health check rejected the candidate ID" >&2
        exit 1
        ;;
esac

if [ -L "$expected_release" ] || [ ! -d "$expected_release" ]; then
    echo "Release health check rejected the candidate directory" >&2
    exit 1
fi

expected_engine="$(readlink -f -- "${expected_release}/bin/cosmo-engine")"
expected_guard="$(readlink -f -- "${expected_release}/lib/${guard_soname}")"
if [ -z "$expected_engine" ] || [ -z "$expected_guard" ]; then
    echo "Release health check cannot resolve the signed compatibility pair" >&2
    exit 1
fi

deadline=$((SECONDS + timeout_seconds))
stable_samples=0
stable_engine_pid=""
while [ "$SECONDS" -lt "$deadline" ]; do
    if ! runner_is_live "$runner_pid"; then
        echo "Candidate startup process exited before health acceptance" >&2
        exit 1
    fi

    engine_pid=""
    for candidate_pid in $(pidof cosmo-engine 2>/dev/null || true); do
        candidate_exe="$(readlink -f -- "/proc/${candidate_pid}/exe" 2>/dev/null || true)"
        if [ "$candidate_exe" = "$expected_engine" ]; then
            if [ -n "$engine_pid" ]; then
                echo "Multiple candidate engine processes are running" >&2
                exit 1
            fi
            engine_pid="$candidate_pid"
        fi
    done

    if [ -n "$engine_pid" ] && [ -r "/proc/${engine_pid}/maps" ]; then
        mapped_guard=0
        while IFS= read -r map_line; do
            case "$map_line" in
                *" ${expected_guard}")
                    mapped_guard=1
                    break
                    ;;
            esac
        done < "/proc/${engine_pid}/maps"

        if [ "$mapped_guard" -eq 1 ] && \
           engine_owns_http_listener "$engine_pid" && \
           engine_http_responds; then
            if [ "$stable_engine_pid" = "$engine_pid" ]; then
                stable_samples=$((stable_samples + 1))
            else
                stable_engine_pid="$engine_pid"
                stable_samples=1
            fi
            if [ "$stable_samples" -ge "$required_stable_samples" ]; then
                exit 0
            fi
        else
            stable_engine_pid=""
            stable_samples=0
        fi
    else
        stable_engine_pid=""
        stable_samples=0
    fi

    sleep 1
done

echo "Candidate did not sustain engine/guard/HTTP health for ${required_stable_samples} consecutive samples within ${timeout_seconds}s" >&2
exit 1
