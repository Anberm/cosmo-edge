#!/usr/bin/env bash
set -euo pipefail

for process_name in nginx srs cosmo-engine; do
    if ! pgrep -x "${process_name}" >/dev/null; then
        echo "process is not running: ${process_name}" >&2
        exit 1
    fi
done

exec 3<>/dev/tcp/127.0.0.1/80
printf 'GET / HTTP/1.0\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n' >&3
IFS= read -r status_line <&3
exec 3>&-
exec 3<&-

if [[ ! "${status_line}" =~ ^HTTP/[0-9.]+[[:space:]]+(2|3)[0-9][0-9][[:space:]] ]]; then
    echo "unexpected web response: ${status_line}" >&2
    exit 1
fi
