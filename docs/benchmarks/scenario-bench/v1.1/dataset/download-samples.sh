#!/usr/bin/env sh
set -eu

expected='ec77182a264f3059a091b68c4973942dba3b80e93f20feaf4d7e146885caf9d2'
output=${1:-'Safety Helmet.mp4'}
url='https://raw.githubusercontent.com/cosmo-wander-ai/cosmo-edge/daeda95ccaf0119d384ff90d5d20c3e90fde8ccb/data/test-video/Safety%20Helmet.mp4'

echo 'The sample is not bundled. Confirm the provenance note in dataset-card.md before redistribution.' >&2
if command -v curl >/dev/null 2>&1; then
  curl -fL --retry 3 -o "$output" "$url"
elif command -v wget >/dev/null 2>&1; then
  wget -O "$output" "$url"
else
  echo 'curl or wget is required' >&2
  exit 1
fi

actual=$(sha256sum "$output" | awk '{print $1}')
if [ "$actual" != "$expected" ]; then
  echo "SHA-256 mismatch: expected $expected, got $actual" >&2
  exit 1
fi
echo "Verified $output"
