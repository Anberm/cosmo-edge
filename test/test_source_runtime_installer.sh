#!/bin/bash
set -euo pipefail

IFS=$' \t\n'
PATH='/usr/sbin:/usr/bin:/sbin:/bin'
export IFS PATH

readonly REPOSITORY_ROOT="$(cd "$(dirname "$0")/.." && pwd -P)"
readonly TEST_ROOT="$(mktemp -d)"
readonly EDGE_COMMIT='1234567890abcdef1234567890abcdef12345678'
readonly GUARD_RELEASE='cmg-sdk-v2.3.3'
trap 'rm -rf -- "$TEST_ROOT"' EXIT

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

assert_absent() {
    [ ! -e "$1" ] && [ ! -L "$1" ] ||
        fail "unexpected path exists: $1"
}

make_mock_systemctl() {
    local destination="$1"
    cat >"$destination" <<'EOF'
#!/bin/bash
set -eu
printf '%s\n' "$*" >>"${COSMO_SOURCE_TEST_SYSTEMCTL_LOG}"
case "${1:-}" in
    stop)
        rm -f -- "${COSMO_SOURCE_TEST_SERVICE_ACTIVE}"
        ;;
    restart | start)
        : >"${COSMO_SOURCE_TEST_SERVICE_ACTIVE}"
        ;;
    enable)
        : >"${COSMO_SOURCE_TEST_SERVICE_ENABLED}"
        ;;
    is-active)
        [ -e "${COSMO_SOURCE_TEST_SERVICE_ACTIVE}" ]
        ;;
    is-enabled)
        [ -e "${COSMO_SOURCE_TEST_SERVICE_ENABLED}" ]
        ;;
esac
EOF
    chmod 0755 "$destination"
}

make_mock_health_check() {
    local destination="$1"
    cat >"$destination" <<'EOF'
#!/bin/bash
set -eu
printf 'health\n' >>"${COSMO_SOURCE_TEST_HEALTH_LOG}"
[ ! -e "${COSMO_SOURCE_TEST_HEALTH_FAIL}" ]
EOF
    chmod 0755 "$destination"
}

make_root() {
    local root="$1"
    mkdir -p "$root/data/cwaiuserdata/model-guard"
    printf 'device-certificate\n' \
        >"$root/data/cwaiuserdata/model-guard/device-certificate.bin"
    printf 'unrelated-state\n' \
        >"$root/data/cwaiuserdata/model-guard/unrelated-state"
    make_mock_systemctl "$root/mock-systemctl"
    make_mock_health_check "$root/mock-health"
    : >"$root/systemctl.log"
    : >"$root/health.log"
}

guard_digest() {
    local guard_root="$1/data/cwaiuserdata/model-guard"
    (
        cd "$guard_root"
        find . -type f -print0 |
            LC_ALL=C sort -z |
            xargs -0 sha256sum
    ) | sha256sum | awk '{print $1}'
}

make_payload() {
    local destination="$1" version="$2" engine_sha build_identity
    mkdir -p \
        "$destination/bin" \
        "$destination/lib" \
        "$destination/resource" \
        "$destination/scripts" \
        "$destination/share/cosmo-model-guard" \
        "$destination/share/cosmo-source"
    cp "$REPOSITORY_ROOT/install-device.sh" "$destination/install-device.sh"
    cp "$REPOSITORY_ROOT/scripts/source_health_check.sh" \
        "$destination/scripts/source_health_check.sh"
    cp "$REPOSITORY_ROOT/config/systemd/cosmo-source.service" \
        "$destination/share/cosmo-source/cosmo.service"
    printf '#!/bin/sh\nexit 0\n' >"$destination/bin/cosmo-engine"
    printf 'V%s\n' "$version" >"$destination/bin/version.txt"
    printf 'runtime-%s\n' "$version" >"$destination/resource/runtime.txt"
    for script in run_start.sh source_run_start.sh stop.sh; do
        printf '#!/bin/sh\nexit 0\n' >"$destination/scripts/$script"
    done
    printf 'guard-%s\n' "$version" \
        >"$destination/lib/libcosmo_model_guard.so.2.0.0"
    cat >"$destination/share/cosmo-model-guard/sdk-release.env" <<EOF
CMG_SDK_RELEASE_FORMAT=cosmo-model-guard-sdk-release-v2
CMG_SDK_RELEASE_ID=${GUARD_RELEASE}
EOF
    engine_sha="$(sha256sum "$destination/bin/cosmo-engine" | awk '{print $1}')"
    build_identity="$(
        printf '%s:%s:%s\n' "$EDGE_COMMIT" "$version" "$engine_sha" |
            sha256sum | awk '{print $1}'
    )"
    cat >"$destination/share/cosmo-source/build-identity.env" <<EOF
format=cosmo-source-build-identity-v1
edge_commit=${EDGE_COMMIT}
version=V${version}
guard_release_id=${GUARD_RELEASE}
engine_sha256=${engine_sha}
build_identity=${build_identity}
EOF
    chmod 0755 \
        "$destination/install-device.sh" \
        "$destination/bin/cosmo-engine" \
        "$destination/scripts/run_start.sh" \
        "$destination/scripts/source_health_check.sh" \
        "$destination/scripts/source_run_start.sh" \
        "$destination/scripts/stop.sh"
}

run_installer() {
    local root="$1" payload="$2"
    shift 2
    COSMO_SOURCE_INSTALL_TESTING=1 \
    COSMO_SOURCE_INSTALL_TEST_ROOT="$root" \
    COSMO_SOURCE_INSTALL_TEST_SYSTEMCTL="$root/mock-systemctl" \
    COSMO_SOURCE_INSTALL_TEST_HEALTH_CHECK="$root/mock-health" \
    COSMO_SOURCE_INSTALL_TEST_HEALTH_TIMEOUT=1 \
    COSMO_SOURCE_TEST_SYSTEMCTL_LOG="$root/systemctl.log" \
    COSMO_SOURCE_TEST_SERVICE_ACTIVE="$root/service-active" \
    COSMO_SOURCE_TEST_SERVICE_ENABLED="$root/service-enabled" \
    COSMO_SOURCE_TEST_HEALTH_LOG="$root/health.log" \
    COSMO_SOURCE_TEST_HEALTH_FAIL="$root/health-fail" \
        "$payload/install-device.sh" "$@"
}

assert_no_backup() {
    local root="$1" found
    found="$(
        find "$root" \
            \( -name '.cosmo-source' -o -name '*backup*' \
            -o -name 'previous-app' -o -name '.cosmo-source-staging.*' \) \
            -print
    )"
    [ -z "$found" ] || fail "installer retained backup/state: $found"
}

test_fresh_install() {
    local root="$TEST_ROOT/fresh" payload="$TEST_ROOT/payload-fresh"
    make_root "$root"
    make_payload "$payload" '1.0.0'
    local before
    before="$(guard_digest "$root")"

    run_installer "$root" "$payload" install

    [ -f "$root/appfs/cosmo_wander/cwai_data/bin/cosmo-engine" ] ||
        fail "fresh install did not create the application"
    [ "$(cat "$root/appfs/cosmo_wander/cwai_data/bin/version.txt")" = 'V1.0.0' ] ||
        fail "fresh install version is wrong"
    [ "$(guard_digest "$root")" = "$before" ] ||
        fail "fresh install changed Guard state"
    assert_no_backup "$root"

    local status
    status="$(run_installer "$root" "$payload" status)"
    grep -Fxq 'mode=source' <<<"$status" || fail "status did not report SOURCE"
    grep -Fxq "guard_release_id=${GUARD_RELEASE}" <<<"$status" ||
        fail "status did not report Guard release"
    grep -Fxq 'service_active=yes' <<<"$status" ||
        fail "status did not report active service"
}

test_existing_and_update_are_replaced_without_backup() {
    local root="$TEST_ROOT/update"
    local first="$TEST_ROOT/payload-update-1"
    local second="$TEST_ROOT/payload-update-2"
    make_root "$root"
    mkdir -p "$root/appfs/cosmo_wander/cwai_data"
    printf 'old-official\n' \
        >"$root/appfs/cosmo_wander/cwai_data/official-only"
    make_payload "$first" '1.0.0'
    make_payload "$second" '2.0.0'
    printf 'first-only\n' >"$first/resource/first-only"
    local before
    before="$(guard_digest "$root")"

    run_installer "$root" "$first" install
    assert_absent "$root/appfs/cosmo_wander/cwai_data/official-only"
    run_installer "$root" "$second" install

    assert_absent "$root/appfs/cosmo_wander/cwai_data/resource/first-only"
    [ "$(cat "$root/appfs/cosmo_wander/cwai_data/bin/version.txt")" = 'V2.0.0' ] ||
        fail "update did not install the second version"
    [ "$(guard_digest "$root")" = "$before" ] ||
        fail "update changed Guard state"
    assert_no_backup "$root"
}

test_permissions_are_not_an_admission_gate() {
    local root="$TEST_ROOT/permissions" payload="$TEST_ROOT/payload-permissions"
    make_root "$root"
    make_payload "$payload" '3.0.0'
    chmod -R a+rwx "$payload"

    run_installer "$root" "$payload" install

    [ "$(cat "$root/appfs/cosmo_wander/cwai_data/bin/version.txt")" = 'V3.0.0' ] ||
        fail "permissive payload was rejected"
    assert_no_backup "$root"
}

test_health_failure_keeps_new_install_without_backup() {
    local root="$TEST_ROOT/health"
    local first="$TEST_ROOT/payload-health-1"
    local second="$TEST_ROOT/payload-health-2"
    make_root "$root"
    make_payload "$first" '1.0.0'
    make_payload "$second" '2.0.0'
    run_installer "$root" "$first" install
    local before
    before="$(guard_digest "$root")"
    : >"$root/health-fail"

    if run_installer "$root" "$second" install; then
        fail "unhealthy install unexpectedly succeeded"
    fi

    [ "$(cat "$root/appfs/cosmo_wander/cwai_data/bin/version.txt")" = 'V2.0.0' ] ||
        fail "health failure restored or retained the old version"
    [ "$(guard_digest "$root")" = "$before" ] ||
        fail "health failure changed Guard state"
    assert_no_backup "$root"
}

test_invalid_payload_does_not_delete_current_application() {
    local root="$TEST_ROOT/invalid" payload="$TEST_ROOT/payload-invalid"
    make_root "$root"
    make_payload "$payload" '4.0.0'
    rm -- "$payload/lib/libcosmo_model_guard.so.2.0.0"
    mkdir -p "$root/appfs/cosmo_wander/cwai_data"
    printf 'keep-until-validation-finishes\n' \
        >"$root/appfs/cosmo_wander/cwai_data/current"

    if run_installer "$root" "$payload" install; then
        fail "invalid payload unexpectedly installed"
    fi
    [ -f "$root/appfs/cosmo_wander/cwai_data/current" ] ||
        fail "invalid payload deleted the current application"
    assert_no_backup "$root"
}

test_unmanaged_status() {
    local root="$TEST_ROOT/unmanaged" payload="$TEST_ROOT/payload-unmanaged"
    make_root "$root"
    make_payload "$payload" '5.0.0'
    local status
    status="$(run_installer "$root" "$payload" status)"
    grep -Fxq 'mode=unmanaged' <<<"$status" ||
        fail "unmanaged status is wrong"
}

test_fresh_install
test_existing_and_update_are_replaced_without_backup
test_permissions_are_not_an_admission_gate
test_health_failure_keeps_new_install_without_backup
test_invalid_payload_does_not_delete_current_application
test_unmanaged_status

echo "SOURCE runtime installer tests passed"
