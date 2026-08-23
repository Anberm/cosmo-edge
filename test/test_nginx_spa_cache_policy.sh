#!/bin/bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd -P)"
config="$repo/nginx/conf/conf.d/default.conf"

# Hashed frontend assets must never fall back to index.html. Returning HTML for
# a missing module makes browsers reject it with a strict MIME type error.
grep -Eq 'location[[:space:]]+\^~[[:space:]]+/assets/' "$config"
grep -Eq 'try_files[[:space:]]+\$uri[[:space:]]+=404;' "$config"
grep -Eq 'Cache-Control[[:space:]]+"public, max-age=31536000, immutable"' "$config"

# The SPA entry point names hashed assets from one specific build. It must be
# re-fetched after an upgrade instead of being reused with a new asset set.
grep -Eq 'location[[:space:]]+=[[:space:]]+/index\.html' "$config"
grep -Eq 'Cache-Control[[:space:]]+"no-store, no-cache, must-revalidate"[[:space:]]+always;' "$config"

echo "nginx SPA cache policy tests passed"
