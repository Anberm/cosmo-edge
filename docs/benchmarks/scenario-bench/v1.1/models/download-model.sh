#!/usr/bin/env sh
set -eu

cat >&2 <<'EOF'
Model binaries are not redistributed by this benchmark package.

Install a licensed platform artifact matching models/<platform>.json, then record
its SHA-256. A different hash is a community reproduction, not a byte-identical
rerun of the frozen evidence.
EOF
exit 1
