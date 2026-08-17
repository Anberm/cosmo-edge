#!/bin/bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd -P)"
root="$(mktemp -d)"
trap 'rm -rf -- "$root"' EXIT

payload="$root/payload"
active="$root/appfs/cosmo_wander/cwai_data"
mkdir -p "$payload/scripts" "$payload/bin" "$payload/files/Interface" \
    "$payload/web" "$active/resource/models" "$active/bin"
cp "$repo/scripts/legacy_migration_install.sh" "$payload/scripts/install.sh"
printf 'new\n' >"$payload/bin/cosmo-engine"
printf '#!/bin/sh\ntouch "$COSMO_MIGRATION_TEST_ROOT/stop.called"\n' >"$payload/scripts/stop.sh"
printf '#!/bin/sh\n' >"$payload/scripts/start.sh"
printf '#!/bin/sh\n' >"$payload/scripts/inte_run_start.sh"
chmod +x "$payload/scripts/"*.sh
printf 'http\n' >"$payload/files/Interface/ai-box-interface_v1.0.html"
printf 'mqtt\n' >"$payload/files/Interface/mqtt_v1.0.html"
printf 'old\n' >"$active/bin/cosmo-engine"
printf 'existing-model\n' >"$active/resource/models/model.nn"
printf 'preserved\n' >"$active/resource/device-local.conf"

COSMO_MIGRATION_TEST_ROOT="$root" \
    sh "$payload/scripts/install.sh" "$root/install.log"

grep -Fxq new "$active/bin/cosmo-engine"
grep -Fxq existing-model "$active/resource/models/model.nn"
grep -Fxq preserved "$active/resource/device-local.conf"
test ! -e "$root/appfs/cosmo_wander/.cosmo-migration-backup"
test -f "$root/stop.called"
test ! -e "$active/scripts/install.sh"
test -d "$active/bin/nginx_conf/logs"
test -L "$active/web/staticfile/httpInterface.html"
test -L "$active/web/staticfile/mqttInterface.html"
test -f "$root/data/cwaiuserdata/mqttUpgradeApp"
service="$root/etc/systemd/system/cosmo.service"
test -f "$service"
grep -Fxq 'ExecStart=/appfs/cosmo_wander/cwai_data/scripts/inte_run_start.sh' "$service"
grep -Fxq 'Restart=on-failure' "$service"
grep -Fxq 'RestartSec=10' "$service"
test -L "$root/etc/systemd/system/multi-user.target.wants/cosmo.service"

# The same permanent MD5 lifecycle must remain valid after the first bridge
# from main; a later package uses the same installer contract.
printf 'newer\n' >"$payload/bin/cosmo-engine"
sed -i 's#/appfs/cosmo_wander/cwai_data#/appfs/minivision/mv_data#' "$service"
COSMO_MIGRATION_TEST_ROOT="$root" \
    sh "$payload/scripts/install.sh" "$root/install-again.log"
grep -Fxq newer "$active/bin/cosmo-engine"
grep -Fxq existing-model "$active/resource/models/model.nn"
grep -Fxq 'ExecStart=/appfs/cosmo_wander/cwai_data/scripts/inte_run_start.sh' "$service"

rm -rf -- "$payload" "$active"
mkdir -p "$payload/scripts" "$payload/bin" "$payload/resource/models" "$active/resource/models"
cp "$repo/scripts/legacy_migration_install.sh" "$payload/scripts/install.sh"
printf 'new\n' >"$payload/bin/cosmo-engine"
printf '#!/bin/sh\n' >"$payload/scripts/stop.sh"
printf '#!/bin/sh\n' >"$payload/scripts/start.sh"
printf '#!/bin/sh\n' >"$payload/scripts/inte_run_start.sh"
chmod +x "$payload/scripts/"*.sh
printf 'packaged-model\n' >"$payload/resource/models/model.nn"
printf 'existing-model\n' >"$active/resource/models/model.nn"
printf 'preserved\n' >"$active/resource/device-local.conf"

COSMO_MIGRATION_TEST_ROOT="$root" \
    sh "$payload/scripts/install.sh" "$root/install.log"

grep -Fxq packaged-model "$active/resource/models/model.nn"
grep -Fxq preserved "$active/resource/device-local.conf"

printf 'stale\n' >"$active/resource/stale.conf"
COSMO_MIGRATION_TEST_ROOT="$root" CLEAN_RESOURCE=1 \
    sh "$payload/scripts/install.sh" "$root/install-clean.log"
test ! -e "$active/resource/stale.conf"
grep -Fxq packaged-model "$active/resource/models/model.nn"

# Preserved resources must be copied into staging before the package overlays
# them. Keeping the packaged resource tree in staging while copying the active
# tree creates an avoidable second model-sized allocation on /appfs.
installer="$repo/scripts/legacy_migration_install.sh"
preserved_copy_line="$(grep -nF 'cp -a -- "${active_root}/resource/." "${staging_root}/resource/"' "$installer" | cut -d: -f1)"
payload_copy_line="$(grep -nF 'cp -a -- "${payload_root}/." "$staging_root/"' "$installer" | cut -d: -f1)"
test -n "$preserved_copy_line"
test -n "$payload_copy_line"
test "$preserved_copy_line" -lt "$payload_copy_line"
! grep -Fq '.packaged-resource' "$installer"
echo "legacy migration installer tests passed"
