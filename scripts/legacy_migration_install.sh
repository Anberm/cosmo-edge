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

script_path="$(readlink -f "$0")"
[ -n "$script_path" ] || fail "cannot resolve installer path"
payload_root="${script_path%/scripts/install.sh}"
[ "$payload_root" != "$script_path" ] || fail "installer is outside the package scripts directory"

if [ -n "${COSMO_MIGRATION_TEST_ROOT:-}" ]; then
    case "$COSMO_MIGRATION_TEST_ROOT" in /*) ;; *) fail "test root must be absolute" ;; esac
    [ "$COSMO_MIGRATION_TEST_ROOT" != / ] || fail "test root is invalid"
    active_root="${COSMO_MIGRATION_TEST_ROOT}/appfs/cosmo_wander/cwai_data"
else
    active_root='/appfs/cosmo_wander/cwai_data'
fi
active_parent="${active_root%/*}"
staging_root="${active_parent}/.cosmo-migration-staging.$$"
backup_root="${active_parent}/.cosmo-migration-backup"

[ -f "${payload_root}/bin/cosmo-engine" ] || fail "package is missing bin/cosmo-engine"
[ -f "${payload_root}/scripts/start.sh" ] || [ -n "${COSMO_MIGRATION_TEST_ROOT:-}" ] ||
    fail "package is missing scripts/start.sh"

mkdir -p -- "$active_parent"
[ ! -e "$staging_root" ] && [ ! -L "$staging_root" ] || fail "staging path already exists"
mkdir -- "$staging_root"
trap 'rm -rf -- "$staging_root"' EXIT
cp -a -- "${payload_root}/." "$staging_root/"

# A model-less migration means preserve, not delete. A package that contains
# resource/models remains authoritative and replaces the installed models.
if [ ! -d "${staging_root}/resource/models" ] && [ -d "${active_root}/resource/models" ]; then
    mkdir -p -- "${staging_root}/resource"
    cp -a -- "${active_root}/resource/models" "${staging_root}/resource/models"
fi

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
sync
printf '[MIGRATION] installed legacy-compatible bridge at %s\n' "$active_root" >>"$log_file"
