#!/bin/sh
set -eu

IFS="$(printf ' \t\n_')"
IFS="${IFS%_}"
PATH='/usr/sbin:/usr/bin:/sbin:/bin'
export IFS PATH

fail() {
    echo "[MIGRATION] ERROR: $*" >&2
    exit 1
}

[ "$#" -le 1 ] || fail "legacy entry accepts only the optional log file"
log_file="${1:-/dev/null}"

log() {
    printf '[INSTALL] %s\n' "$*" | tee -a "$log_file"
}

script_path="$(readlink -f "$0")"
[ -n "$script_path" ] || fail "cannot resolve installer path"
payload_root="${script_path%/scripts/install.sh}"
[ "$payload_root" != "$script_path" ] || fail "installer is outside the package scripts directory"

if [ -n "${COSMO_MIGRATION_TEST_ROOT:-}" ]; then
    case "$COSMO_MIGRATION_TEST_ROOT" in /*) ;; *) fail "test root must be absolute" ;; esac
    [ "$COSMO_MIGRATION_TEST_ROOT" != / ] || fail "test root is invalid"
    active_root="${COSMO_MIGRATION_TEST_ROOT}/appfs/cosmo_wander/cwai_data"
    systemd_root="${COSMO_MIGRATION_TEST_ROOT}/etc/systemd/system"
    upgrade_sign="${COSMO_MIGRATION_TEST_ROOT}/data/cwaiuserdata/mqttUpgradeApp"
else
    active_root='/appfs/cosmo_wander/cwai_data'
    systemd_root='/etc/systemd/system'
    upgrade_sign='/data/cwaiuserdata/mqttUpgradeApp'
fi
active_parent="${active_root%/*}"
staging_root="${active_parent}/.cosmo-migration-staging.$$"
backup_root="${active_parent}/.cosmo-migration-backup"

[ -f "${payload_root}/bin/cosmo-engine" ] || fail "package is missing bin/cosmo-engine"
for required_script in stop.sh start.sh inte_run_start.sh; do
    [ -x "${payload_root}/scripts/${required_script}" ] ||
        fail "package script is missing or not executable: ${required_script}"
done

log "Install Start"
log "script=${script_path}, logFile=${log_file}"
log "Stopping active Cosmo processes"
"${payload_root}/scripts/stop.sh"

mkdir -p -- "$active_parent"
[ ! -e "$staging_root" ] && [ ! -L "$staging_root" ] || fail "staging path already exists"
mkdir -- "$staging_root"
trap 'rm -rf -- "$staging_root"' EXIT

# Match the historical installer: preserve the installed resource tree by
# default, then overlay packaged resources. Copying the installed tree first
# avoids keeping a second packaged-resource copy on space-constrained /appfs.
# CLEAN_RESOURCE=1 makes the package resource tree authoritative.
if [ "${CLEAN_RESOURCE:-0}" != 1 ] && [ -d "${active_root}/resource" ]; then
    mkdir -- "${staging_root}/resource"
    cp -a -- "${active_root}/resource/." "${staging_root}/resource/"
fi
cp -a -- "${payload_root}/." "$staging_root/"

[ ! -e "$backup_root" ] && [ ! -L "$backup_root" ] || fail "stale migration backup exists"
if [ -e "$active_root" ] || [ -L "$active_root" ]; then
    mv -- "$active_root" "$backup_root"
fi
if ! mv -- "$staging_root" "$active_root"; then
    [ ! -e "$backup_root" ] || mv -- "$backup_root" "$active_root"
    fail "cannot activate migrated application"
fi
trap - EXIT
rm -rf -- "$backup_root"

# Preserve the historical post-install filesystem contract.
mkdir -p -- "${active_root}/web/staticfile" "${active_root}/bin/nginx_conf/logs"
rm -f -- \
    "${active_root}/web/staticfile/httpInterface.html" \
    "${active_root}/web/staticfile/mqttInterface.html"
ln -s -- \
    "${active_root}/files/Interface/ai-box-interface_v1.0.html" \
    "${active_root}/web/staticfile/httpInterface.html"
ln -s -- \
    "${active_root}/files/Interface/mqtt_v1.0.html" \
    "${active_root}/web/staticfile/mqttInterface.html"
rm -f -- "${active_root}/scripts/install.sh"

# Always replace the unit. Existing main installations may still point to the
# former /appfs/minivision/mv_data tree, while a deleted unit must be recreated.
mkdir -p -- "$systemd_root"
service_file="${systemd_root}/cosmo.service"
service_temp="${service_file}.tmp.$$"
trap 'rm -f -- "$service_temp"' EXIT
umask 022
{
    printf '%s\n' \
        '[Unit]' \
        'Description=Cosmo Edge AI Engine' \
        'Wants=network-online.target' \
        'After=network-online.target docker.service' \
        '' \
        '[Service]' \
        'Type=simple' \
        'User=root' \
        'ExecStart=/appfs/cosmo_wander/cwai_data/scripts/inte_run_start.sh' \
        'Restart=on-failure' \
        'RestartSec=10' \
        '' \
        '[Install]' \
        'WantedBy=multi-user.target'
} >"$service_temp"
chmod 0644 "$service_temp"
mv -f -- "$service_temp" "$service_file"
trap - EXIT

if [ -n "${COSMO_MIGRATION_TEST_ROOT:-}" ]; then
    wants_dir="${systemd_root}/multi-user.target.wants"
    mkdir -p -- "$wants_dir"
    ln -sfn ../cosmo.service "${wants_dir}/cosmo.service"
else
    systemctl daemon-reload
    systemctl enable cosmo.service
fi

mkdir -p -- "${upgrade_sign%/*}"
: >"$upgrade_sign"
sync
log "Install files Done"
log "systemd service [cosmo] installed and enabled"
log "installed legacy-compatible bridge at ${active_root}"
log "Install End"
