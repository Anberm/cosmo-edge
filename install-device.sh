#!/bin/bash
set -euo pipefail

IFS=$' \t\n'
PATH='/usr/sbin:/usr/bin:/sbin:/bin'
export IFS PATH

readonly SERVICE_NAME='cosmo.service'

fail() {
    echo "[SOURCE-INSTALL] ERROR: $*" >&2
    exit 1
}

log() {
    echo "[SOURCE-INSTALL] $*"
}

if [ "${COSMO_SOURCE_INSTALL_TESTING:-}" = '1' ]; then
    test_root="${COSMO_SOURCE_INSTALL_TEST_ROOT:-}"
    case "$test_root" in
        /*) ;;
        *) fail "test root must be absolute" ;;
    esac
    [ "$test_root" != '/' ] && [ -d "$test_root" ] ||
        fail "test root is invalid"
    active_root="${test_root}/appfs/cosmo_wander/cwai_data"
    service_unit="${test_root}/etc/systemd/system/${SERVICE_NAME}"
    systemctl_command="${COSMO_SOURCE_INSTALL_TEST_SYSTEMCTL:-}"
    health_check_command="${COSMO_SOURCE_INSTALL_TEST_HEALTH_CHECK:-}"
    health_timeout="${COSMO_SOURCE_INSTALL_TEST_HEALTH_TIMEOUT:-1}"
    [ -x "$systemctl_command" ] || fail "test systemctl is unavailable"
    [ -x "$health_check_command" ] || fail "test health check is unavailable"
else
    active_root='/appfs/cosmo_wander/cwai_data'
    service_unit="/etc/systemd/system/${SERVICE_NAME}"
    health_check_command=''
    health_timeout=30
    if [ -x /usr/bin/systemctl ]; then
        systemctl_command='/usr/bin/systemctl'
    elif [ -x /bin/systemctl ]; then
        systemctl_command='/bin/systemctl'
    else
        fail "systemctl is unavailable"
    fi
fi

case "$health_timeout" in
    '' | *[!0-9]*) fail "health timeout must be a positive integer" ;;
esac
[ "$health_timeout" -gt 0 ] || fail "health timeout must be positive"

readonly active_root service_unit systemctl_command health_check_command
readonly health_timeout
readonly active_parent="${active_root%/*}"
readonly staging_root="${active_parent}/.cosmo-source-staging.$$"

script_path="$(readlink -f -- "$0")"
[ -n "$script_path" ] || fail "cannot resolve installer path"
readonly payload_root="${script_path%/*}"

systemctl_run() {
    "$systemctl_command" "$@"
}

manifest_value() {
    local file="$1" key="$2"
    awk -F= -v key="$key" '
        $1 == key {
            if (found || NF < 2) {
                exit 2
            }
            value = substr($0, length(key) + 2)
            found = 1
        }
        END {
            if (!found || value == "") {
                exit 1
            }
            print value
        }
    ' "$file"
}

validate_payload() {
    local path
    for path in \
        bin/cosmo-engine \
        bin/version.txt \
        lib/libcosmo_model_guard.so.2.0.0 \
        scripts/run_start.sh \
        scripts/source_run_start.sh \
        scripts/source_health_check.sh \
        scripts/stop.sh \
        share/cosmo-model-guard/sdk-release.env \
        share/cosmo-source/build-identity.env \
        share/cosmo-source/cosmo.service
    do
        [ -f "${payload_root}/${path}" ] ||
            fail "SOURCE payload is missing ${path}"
    done
    for path in \
        bin/cosmo-engine \
        scripts/run_start.sh \
        scripts/source_run_start.sh \
        scripts/source_health_check.sh \
        scripts/stop.sh
    do
        [ -x "${payload_root}/${path}" ] ||
            fail "SOURCE payload is not executable: ${path}"
    done

    local sdk_release identity_release
    sdk_release="$(
        manifest_value \
            "${payload_root}/share/cosmo-model-guard/sdk-release.env" \
            CMG_SDK_RELEASE_ID
    )" || fail "Guard SDK release ID is invalid"
    identity_release="$(
        manifest_value \
            "${payload_root}/share/cosmo-source/build-identity.env" \
            guard_release_id
    )" || fail "SOURCE build identity is invalid"
    [ "$sdk_release" = "$identity_release" ] ||
        fail "SOURCE build identity and Guard SDK differ"
}

prepare_staging() {
    mkdir -p -- "$active_parent"
    [ ! -e "$staging_root" ] && [ ! -L "$staging_root" ] ||
        fail "staging path already exists: ${staging_root}"
    mkdir -- "$staging_root"
    cp -a -- "${payload_root}/." "${staging_root}/"
}

cleanup_staging() {
    if [ -n "${staging_root:-}" ] &&
        { [ -e "$staging_root" ] || [ -L "$staging_root" ]; }; then
        rm -rf -- "$staging_root"
    fi
}

start_and_check() {
    local health_script="$1" attempt=0
    systemctl_run daemon-reload
    systemctl_run enable "$SERVICE_NAME"
    systemctl_run restart "$SERVICE_NAME"
    while [ "$attempt" -lt "$health_timeout" ]; do
        if systemctl_run is-active --quiet "$SERVICE_NAME" &&
            "$health_script"; then
            return 0
        fi
        attempt=$((attempt + 1))
        [ "$attempt" -ge "$health_timeout" ] || sleep 1
    done
    return 1
}

install_action() {
    validate_payload
    prepare_staging
    trap cleanup_staging EXIT

    systemctl_run stop "$SERVICE_NAME" >/dev/null 2>&1 || true
    rm -rf -- "$active_root"
    mv -- "$staging_root" "$active_root"
    trap - EXIT

    mkdir -p -- "${service_unit%/*}"
    cp -- "${active_root}/share/cosmo-source/cosmo.service" "$service_unit"

    local health_script="${active_root}/scripts/source_health_check.sh"
    if [ -n "$health_check_command" ]; then
        health_script="$health_check_command"
    fi
    if ! start_and_check "$health_script"; then
        fail "SOURCE runtime was installed but failed its health check"
    fi

    local build_id edge_commit guard_release
    build_id="$(
        manifest_value \
            "${active_root}/share/cosmo-source/build-identity.env" \
            build_identity
    )"
    edge_commit="$(
        manifest_value \
            "${active_root}/share/cosmo-source/build-identity.env" \
            edge_commit
    )"
    guard_release="$(
        manifest_value \
            "${active_root}/share/cosmo-source/build-identity.env" \
            guard_release_id
    )"
    log "SOURCE runtime installed at ${active_root}"
    log "build_id=${build_id} edge_commit=${edge_commit} guard_release_id=${guard_release}"
    log "No application backup was created; Guard certificate state was not accessed"
}

print_service_status() {
    if systemctl_run is-active --quiet "$SERVICE_NAME" >/dev/null 2>&1; then
        echo 'service_active=yes'
    else
        echo 'service_active=no'
    fi
    if systemctl_run is-enabled --quiet "$SERVICE_NAME" >/dev/null 2>&1; then
        echo 'service_enabled=yes'
    else
        echo 'service_enabled=no'
    fi
}

status_action() {
    local identity="${active_root}/share/cosmo-source/build-identity.env"
    if [ ! -f "$identity" ]; then
        echo 'mode=unmanaged'
        print_service_status
        return 0
    fi
    echo 'mode=source'
    for key in edge_commit version guard_release_id build_identity; do
        printf '%s=%s\n' "$key" "$(manifest_value "$identity" "$key")"
    done
    print_service_status
}

usage() {
    echo "Usage: $0 {install|status}" >&2
}

[ "$#" -eq 1 ] || {
    usage
    exit 2
}

case "$1" in
    install) install_action ;;
    status) status_action ;;
    *)
        usage
        exit 2
        ;;
esac
