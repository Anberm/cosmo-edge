#!/bin/bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <web-workspace>" >&2
    exit 2
fi

web_workspace="$1"
if [ ! -f "${web_workspace}/package-lock.json" ]; then
    echo "ERROR: package-lock.json not found in ${web_workspace}" >&2
    exit 1
fi

cd "${web_workspace}"
npm_ci_args=(ci --offline --include=dev --loglevel=error --no-audit --no-fund)

if npm "${npm_ci_args[@]}" >/dev/null 2>&1; then
    echo "npm dependencies installed from the persistent offline cache."
    exit 0
fi

package_url_output="$(node <<'NODE'
const lock = require('./package-lock.json');
const urls = [];
const incomplete = [];

for (const [path, metadata] of Object.entries(lock.packages || {})) {
  if (!path.includes('node_modules/') || metadata.link) continue;
  if (!metadata.resolved || !metadata.integrity) {
    incomplete.push(path);
    continue;
  }
  if (!metadata.resolved.startsWith('https://cdn.npmmirror.com/packages/')) {
    throw new Error(`unsupported package URL for ${path}: ${metadata.resolved}`);
  }
  urls.push(metadata.resolved);
}

if (incomplete.length) {
  throw new Error(`package-lock.json has incomplete entries: ${incomplete.join(', ')}`);
}
if (!urls.length) throw new Error('package-lock.json has no cacheable packages');
process.stdout.write([...new Set(urls)].join('\n'));
NODE
)"

mapfile -t package_urls <<<"${package_url_output}"
package_count="${#package_urls[@]}"
echo "npm cache is incomplete; fetching ${package_count} locked packages serially."

for index in "${!package_urls[@]}"; do
    npm --loglevel=error --no-audit --no-fund cache add "${package_urls[$index]}"
    completed=$((index + 1))
    if (( completed % 10 == 0 || completed == package_count )); then
        echo "npm cache ${completed}/${package_count}"
    fi
done

# The online phase only populates the content-addressed cache. Installation is
# always offline and still verifies every package against package-lock.json.
npm "${npm_ci_args[@]}"
echo "npm dependencies installed from the newly populated offline cache."
