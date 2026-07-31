#!/bin/bash
set -eu
IFS=$' \t\n'
PATH='/usr/sbin:/usr/bin:/sbin:/bin'
export IFS PATH

# Boot/start orchestrator for signed, versioned releases.  Upgrade archives are
# parsed and verified only by the updater from the currently trusted release.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd -P)"

# shellcheck source=common.sh
. "${SCRIPT_DIR}/common.sh"

legacy_install_path="${COSMO_INSTALL_DIR}"
ensure_runtime_dirs

mv -f "${COSMO_LOG_DIR}/nginx_access.log" "${COSMO_LOG_DIR}/nginx_access_last.log" 2>/dev/null || true
mv -f "${COSMO_LOG_DIR}/nginx_error.log" "${COSMO_LOG_DIR}/nginx_error_last.log" 2>/dev/null || true

now_log_file="${COSMO_LOG_DIR}/INTE_RUN_now.1"
for candidate_log in "${COSMO_LOG_DIR}"/INTE_RUN_now.*; do
    if [ -f "$candidate_log" ]; then
        now_log_file="$candidate_log"
    fi
done
now_index="${now_log_file##*.}"
case "$now_index" in
    ''|*[!0-9]*) now_index=1 ;;
esac
next_index=$((now_index + 1))
if [ "$next_index" -gt 10 ]; then
    next_index=1
fi
if [ -f "$now_log_file" ]; then
    mv -f "$now_log_file" "${COSMO_LOG_DIR}/INTE_RUN.${now_index}"
    now_log_file="${COSMO_LOG_DIR}/INTE_RUN_now.${next_index}"
fi

log_tag="INTE_RUN"
log_file="$now_log_file"
action="${1:-}"
cosmo_log "$log_tag" "Start action=${action}" "$log_file"

rm -f "$COSMO_UPGRADE_SIGN"
if [ -f "$COSMO_HW_UPGRADE_SIGN" ]; then
    mv -f "$COSMO_HW_UPGRADE_SIGN" "$COSMO_UPGRADE_SIGN"
    cosmo_log "$log_tag" "Hardware upgrade marker converted" "$log_file"
fi

if [ "$action" = "stop" ]; then
    "${SCRIPT_DIR}/stop.sh"
    exit 0
fi
if [ "$action" != "start" ]; then
    cosmo_log "$log_tag" "Unsupported action: ${action}" "$log_file"
    exit 2
fi

run_foreground() {
    release_root="$1"
    cosmo_log "$log_tag" "Starting active release $(basename "$release_root")" "$log_file"
    cd "${release_root}/scripts"
    INSTALLPATH="$release_root" exec "${release_root}/scripts/run_start.sh" start "$log_file"
}

run_candidate_with_health_gate() {
    release_root="$1"
    archive="$2"
    if ! "${SCRIPT_DIR}/install.sh" pending-health-script >/dev/null 2>> "$log_file"; then
        cosmo_log "$log_tag" "Candidate health script validation failed; rolling back" "$log_file"
        previous_release="$("${SCRIPT_DIR}/install.sh" rollback 2>> "$log_file")"
        cosmo_log "$log_tag" "Rollback restored $(basename "$previous_release")" "$log_file"
        run_foreground "$previous_release"
    fi
    cosmo_log "$log_tag" "Starting candidate release $(basename "$release_root")" "$log_file"
    (
        cd "${release_root}/scripts"
        INSTALLPATH="$release_root" exec "${release_root}/scripts/run_start.sh" start "$log_file"
    ) &
    runner_pid=$!

    if "${SCRIPT_DIR}/install.sh" run-pending-health \
        "$runner_pid" "$release_root" >> "$log_file" 2>&1; then
        if "${SCRIPT_DIR}/install.sh" commit-healthy >> "$log_file" 2>&1; then
            mkdir -p "$(dirname "$COSMO_UPGRADE_SIGN")"
            : > "$COSMO_UPGRADE_SIGN"
            sync
            rm -f -- "$archive"
            cosmo_log "$log_tag" "Candidate startup accepted and release committed" "$log_file"
            wait "$runner_pid"
            return $?
        fi
        cosmo_log "$log_tag" "Durable release commit failed; rolling back" "$log_file"
    else
        cosmo_log "$log_tag" "Candidate startup health failed; rolling back" "$log_file"
    fi

    # The currently trusted updater validates and executes the signed candidate
    # stop script before it reverses the pointer or removes the incoming tree.
    previous_release="$("${SCRIPT_DIR}/install.sh" rollback 2>> "$log_file")"
    cosmo_log "$log_tag" "Rollback restored $(basename "$previous_release")" "$log_file"
    run_foreground "$previous_release"
}

find_signed_release_archive() {
    signed_archive_count=0
    signed_archive=""
    for candidate in "${COSMO_UPGRADE_DIR}"/*.tar.gz; do
        [ -f "$candidate" ] || continue
        signed_archive_count=$((signed_archive_count + 1))
        signed_archive="$candidate"
    done
}

release_state="${COSMO_RELEASE_STATE_DIR}/compatibility.state.json"
if [ ! -f "$release_state" ]; then
    # First-release recovery and install always enter through the stable,
    # embedded-key verifier outside the movable facades.  An interrupted
    # migration is reconciled before inspecting a new archive.
    factory_bootstrap="${COSMO_INSTALL_DIR}/.release-bootstrap/bin/cosmo-release-bootstrap"
    bootstrap_journal="${COSMO_RELEASE_STATE_DIR}/bootstrap-transaction.json"
    if [ -f "$bootstrap_journal" ]; then
        if [ ! -x "$factory_bootstrap" ]; then
            cosmo_log "$log_tag" "Factory recovery journal exists but stable verifier is unavailable" "$log_file"
            exit 1
        fi
        cosmo_log "$log_tag" "Recovering interrupted factory release migration" "$log_file"
        if ! "$factory_bootstrap" recover >> "$log_file" 2>&1; then
            cosmo_log "$log_tag" "Factory release recovery failed closed" "$log_file"
            exit 1
        fi
        if [ -f "$release_state" ]; then
            exec "${COSMO_RELEASE_CURRENT}/scripts/start.sh" start
        fi
    fi

    find_signed_release_archive

    if [ "$signed_archive_count" -gt 1 ]; then
        cosmo_log "$log_tag" "Ambiguous first-release archive set; retaining legacy release" "$log_file"
        run_foreground "$legacy_install_path"
    fi
    if [ "$signed_archive_count" -eq 1 ]; then
        if [ ! -x "$factory_bootstrap" ]; then
            cosmo_log "$log_tag" "Signed release found but stable embedded-key verifier is absent" "$log_file"
            run_foreground "$legacy_install_path"
        fi
        cosmo_log "$log_tag" "Installing first signed compatibility release" "$log_file"
        if "$factory_bootstrap" install "$signed_archive" >> "$log_file" 2>&1; then
            rm -f -- "$signed_archive"
            sync
            if [ ! -f "$release_state" ] || [ ! -x "${COSMO_RELEASE_CURRENT}/scripts/start.sh" ]; then
                cosmo_log "$log_tag" "Factory verifier returned without a complete active release" "$log_file"
                exit 1
            fi
            exec "${COSMO_RELEASE_CURRENT}/scripts/start.sh" start
        fi
        cosmo_log "$log_tag" "First signed release rejected; recovering legacy release" "$log_file"
        if [ -f "$bootstrap_journal" ] && ! "$factory_bootstrap" recover >> "$log_file" 2>&1; then
            cosmo_log "$log_tag" "Factory rollback failed closed" "$log_file"
            exit 1
        fi
    fi
    run_foreground "$legacy_install_path"
fi

if ! active_release="$("${SCRIPT_DIR}/install.sh" recover 2>> "$log_file")"; then
    cosmo_log "$log_tag" "Release recovery failed closed" "$log_file"
    exit 1
fi

find_signed_release_archive

if [ "$signed_archive_count" -eq 0 ]; then
    run_foreground "$active_release"
fi
if [ "$signed_archive_count" -ne 1 ]; then
    cosmo_log "$log_tag" "Ambiguous release archive set; exactly one signed archive is required" "$log_file"
    run_foreground "$active_release"
fi

cosmo_log "$log_tag" "Preflighting signed release archive" "$log_file"
if ! "${SCRIPT_DIR}/install.sh" prepare "$signed_archive" "$log_file" >> "$log_file" 2>&1; then
    cosmo_log "$log_tag" "Release archive rejected; retaining active release" "$log_file"
    run_foreground "$active_release"
fi

"${active_release}/scripts/stop.sh"
if ! candidate_release="$("${SCRIPT_DIR}/install.sh" activate 2>> "$log_file")"; then
    cosmo_log "$log_tag" "Release activation failed; recovering previous release" "$log_file"
    active_release="$("${SCRIPT_DIR}/install.sh" recover)"
    run_foreground "$active_release"
fi

run_candidate_with_health_gate "$candidate_release" "$signed_archive"
