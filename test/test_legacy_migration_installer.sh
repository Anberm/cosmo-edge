#!/bin/bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd -P)"
root="$(mktemp -d)"
trap 'rm -rf -- "$root"' EXIT

payload="$root/payload"
active="$root/appfs/cosmo_wander/cwai_data"
mkdir -p "$payload/scripts" "$payload/bin" "$active/resource/models" "$active/bin"
cp "$repo/scripts/legacy_migration_install.sh" "$payload/scripts/install.sh"
printf 'new\n' >"$payload/bin/cosmo-engine"
printf 'old\n' >"$active/bin/cosmo-engine"
printf 'existing-model\n' >"$active/resource/models/model.nn"

COSMO_MIGRATION_TEST_ROOT="$root" \
    "$payload/scripts/install.sh" "$root/install.log"

grep -Fxq new "$active/bin/cosmo-engine"
grep -Fxq existing-model "$active/resource/models/model.nn"
test ! -e "$root/appfs/cosmo_wander/.cosmo-migration-backup"

# The same permanent MD5 lifecycle must remain valid after the first bridge
# from main; a later package uses the same installer contract.
printf 'newer\n' >"$payload/bin/cosmo-engine"
COSMO_MIGRATION_TEST_ROOT="$root" \
    "$payload/scripts/install.sh" "$root/install-again.log"
grep -Fxq newer "$active/bin/cosmo-engine"
grep -Fxq existing-model "$active/resource/models/model.nn"

rm -rf -- "$payload" "$active"
mkdir -p "$payload/scripts" "$payload/bin" "$payload/resource/models" "$active/resource/models"
cp "$repo/scripts/legacy_migration_install.sh" "$payload/scripts/install.sh"
printf 'new\n' >"$payload/bin/cosmo-engine"
printf 'packaged-model\n' >"$payload/resource/models/model.nn"
printf 'existing-model\n' >"$active/resource/models/model.nn"

COSMO_MIGRATION_TEST_ROOT="$root" \
    "$payload/scripts/install.sh" "$root/install.log"

grep -Fxq packaged-model "$active/resource/models/model.nn"
echo "legacy migration installer tests passed"
