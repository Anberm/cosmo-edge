#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
COMPOSE_FILE="${PROJECT_ROOT}/docker-compose.x86.macos.yml"
SERVICE_NAME="cosmo-x86-macos"
CONTAINER_NAME="cosmo-x86-macos-preview"
IMAGE_NAME="cosmo:x86-macos-preview"
WEB_PORT="${COSMO_X86_WEB_PORT:-8080}"
BUILD_JOBS="${COSMO_X86_BUILD_JOBS:-1}"
DOCKER_BIN=""

usage() {
    cat <<'USAGE'
Usage: ./scripts/macos-docker-preview.sh <command>

Commands:
  doctor          Check the Mac, Docker Desktop, Compose, capacity, and ports.
  up              Start the Preview, building only when the image is missing.
  up --build      Rebuild the image, start the Preview, and wait for health.
  status          Show the Compose service and health state.
  logs            Print the most recent service logs.
  logs --follow   Follow service logs until interrupted.
  down            Stop the Preview while preserving its named volumes.
  url             Print the local web-console URL.
  help            Show this help.

Environment:
  COSMO_X86_WEB_PORT   Host web port (default: 8080, range: 1024-65535;
                       media ports 1936, 1985, and 18088 are reserved).
  COSMO_X86_BUILD_JOBS Build parallelism (default: 1 for reliable emulation).
USAGE
}

info() {
    printf '[INFO] %s\n' "$*"
}

pass() {
    printf '[PASS] %s\n' "$*"
}

warn() {
    printf '[WARN] %s\n' "$*" >&2
}

fail() {
    printf '[FAIL] %s\n' "$*" >&2
    exit 1
}

find_docker() {
    local app_bin_dir="/Applications/Docker.app/Contents/Resources/bin"
    if [[ -d "${app_bin_dir}" ]]; then
        # A first-run or per-user Docker Desktop install may not create global
        # CLI and credential-helper symlinks. Keep the adjustment process-local.
        export PATH="${app_bin_dir}:${PATH}"
    fi

    if command -v docker >/dev/null 2>&1; then
        DOCKER_BIN="$(command -v docker)"
        return 0
    fi

    local app_cli="${app_bin_dir}/docker"
    if [[ -x "${app_cli}" ]]; then
        DOCKER_BIN="${app_cli}"
        return 0
    fi

    fail "Docker CLI was not found. Install and finish starting Docker Desktop."
}

docker_cmd() {
    "${DOCKER_BIN}" "$@"
}

compose() {
    docker_cmd compose -f "${COMPOSE_FILE}" "$@"
}

validate_web_port() {
    if [[ ! "${WEB_PORT}" =~ ^[0-9]+$ ]] || ((WEB_PORT < 1024 || WEB_PORT > 65535)); then
        fail "COSMO_X86_WEB_PORT must be an integer from 1024 through 65535; got '${WEB_PORT}'."
    fi
    case "${WEB_PORT}" in
        1936|1985|18088)
            fail "COSMO_X86_WEB_PORT ${WEB_PORT} is reserved for Preview media services."
            ;;
    esac
}

validate_build_jobs() {
    if [[ ! "${BUILD_JOBS}" =~ ^[1-9][0-9]*$ ]]; then
        fail "COSMO_X86_BUILD_JOBS must be a positive integer; got '${BUILD_JOBS}'."
    fi
}

require_apple_silicon_mac() {
    local host_os host_arch
    host_os="$(uname -s)"
    host_arch="$(uname -m)"
    if [[ "${host_os}" != "Darwin" || "${host_arch}" != "arm64" ]]; then
        fail "This Preview is currently admitted only on Apple Silicon macOS; found ${host_os}/${host_arch}."
    fi
    pass "Apple Silicon macOS detected (${host_arch})."
}

check_docker() {
    find_docker
    pass "Docker CLI: $(docker_cmd --version)"

    if ! docker_cmd compose version >/dev/null 2>&1; then
        fail "Docker Compose V2 is unavailable. Finish Docker Desktop setup and retry."
    fi
    pass "Docker Compose: $(docker_cmd compose version --short 2>/dev/null || docker_cmd compose version)"

    if ! docker_cmd info >/dev/null 2>&1; then
        fail "Docker Desktop is not ready. Open Docker Desktop, finish its first-run setup, and retry."
    fi

    local server_platform
    server_platform="$(docker_cmd version --format '{{.Server.Os}}/{{.Server.Arch}}' 2>/dev/null || true)"
    if [[ "${server_platform}" != linux/* ]]; then
        fail "The active Docker server must run Linux containers; found '${server_platform:-unknown}'."
    fi
    pass "Docker server is ready (${server_platform})."

    if ! compose config --quiet; then
        fail "${COMPOSE_FILE} is not valid for the active Compose version."
    fi
    pass "macOS Compose configuration is valid."
}

check_rosetta() {
    if /usr/sbin/pkgutil --pkg-info com.apple.pkg.RosettaUpdateAuto >/dev/null 2>&1 ||
        /usr/bin/pgrep -q oahd 2>/dev/null; then
        pass "Rosetta 2 is installed."
    else
        warn "Rosetta 2 was not detected. amd64 emulation may be significantly slower."
    fi
}

check_capacity() {
    local disk_kb disk_gib docker_bytes docker_mib
    disk_kb="$(df -Pk "${PROJECT_ROOT}" | awk 'NR == 2 {print $4}')"
    disk_gib=$((disk_kb / 1024 / 1024))
    if ((disk_gib < 20)); then
        warn "Only ${disk_gib} GiB is free. The first amd64 build can need substantial temporary space."
    else
        pass "Free host disk: ${disk_gib} GiB."
    fi

    docker_bytes="$(docker_cmd info --format '{{.MemTotal}}' 2>/dev/null || printf '0')"
    if [[ "${docker_bytes}" =~ ^[0-9]+$ ]]; then
        docker_mib=$((docker_bytes / 1024 / 1024))
        # Docker reports usable guest memory, which is slightly below the value
        # selected in Desktop. A nominal 8 GiB allocation is about 7.7 GiB here.
        if ((docker_mib < 7168)); then
            warn "Docker exposes ${docker_mib} MiB RAM. Allocate at least 8 GiB in Docker Desktop for this emulated build."
        else
            pass "Docker usable memory: ${docker_mib} MiB (compatible with the recommended 8 GiB allocation)."
        fi
    fi
}

container_is_running() {
    [[ "$(docker_cmd inspect --format '{{.State.Running}}' "${CONTAINER_NAME}" 2>/dev/null || true)" == "true" ]]
}

tcp_port_in_use() {
    /usr/sbin/lsof -nP -iTCP:"$1" -sTCP:LISTEN -t 2>/dev/null | grep -q .
}

check_ports() {
    if container_is_running; then
        local current_web_port
        current_web_port="$(docker_cmd port "${CONTAINER_NAME}" 80/tcp 2>/dev/null | awk -F: 'NR == 1 {print $NF}')"
        if [[ "${current_web_port}" == "${WEB_PORT}" ]]; then
            pass "Preview container is already running with the requested web port ${WEB_PORT}."
            return 0
        fi
        if tcp_port_in_use "${WEB_PORT}"; then
            fail "Requested web port ${WEB_PORT} is already in use; the running Preview currently uses ${current_web_port:-an unknown port}."
        fi
        pass "Requested web port ${WEB_PORT} is available; Compose will replace the current ${current_web_port:-unknown} binding."
        return 0
    fi

    local port
    for port in "${WEB_PORT}" 1936 1985 18088; do
        if tcp_port_in_use "${port}"; then
            fail "TCP port ${port} is already in use. Stop that service or set COSMO_X86_WEB_PORT for a web-port conflict."
        fi
    done
    pass "Required localhost ports are available."
}

doctor() {
    require_apple_silicon_mac
    validate_web_port
    validate_build_jobs
    check_docker
    check_rosetta
    check_capacity
    check_ports
    pass "Build parallelism: ${BUILD_JOBS} job(s)."
    pass "macOS Docker Preview admission passed."
}

wait_until_healthy() {
    local deadline=$((SECONDS + 240))
    local state=""
    while ((SECONDS < deadline)); do
        state="$(docker_cmd inspect --format '{{if .State.Health}}{{.State.Health.Status}}{{else}}{{.State.Status}}{{end}}' "${CONTAINER_NAME}" 2>/dev/null || true)"
        case "${state}" in
            healthy)
                pass "CosmoEdge is healthy."
                return 0
                ;;
            unhealthy|exited|dead)
                compose logs --tail=200 "${SERVICE_NAME}" >&2 || true
                fail "CosmoEdge entered state '${state}'."
                ;;
        esac
        sleep 5
    done

    compose logs --tail=200 "${SERVICE_NAME}" >&2 || true
    fail "CosmoEdge did not become healthy within 240 seconds (last state: ${state:-unknown})."
}

preview_image_ready() {
    local image_platform
    image_platform="$(docker_cmd image inspect --format '{{.Os}}/{{.Architecture}}' "${IMAGE_NAME}" 2>/dev/null || true)"
    if [[ -z "${image_platform}" ]]; then
        return 1
    fi
    if [[ "${image_platform}" != "linux/amd64" ]]; then
        warn "Existing ${IMAGE_NAME} is ${image_platform}; rebuilding the required linux/amd64 image."
        return 1
    fi
    return 0
}

start_preview() {
    local force_build="${1:-}"
    doctor
    mkdir -p "${PROJECT_ROOT}/build_output/macos-x86"

    if [[ "${force_build}" == "--build" ]] || ! preview_image_ready; then
        info "Building linux/amd64 on Apple Silicon. The first build can take considerably longer than x86 Linux."
        compose up -d --build "${SERVICE_NAME}"
    else
        info "Reusing existing ${IMAGE_NAME}; use 'up --build' after source or build-input changes."
        compose up -d --no-build "${SERVICE_NAME}"
    fi
    wait_until_healthy
    printf 'Web console: http://127.0.0.1:%s\n' "${WEB_PORT}"
}

require_ready_docker() {
    find_docker
    if ! docker_cmd info >/dev/null 2>&1; then
        fail "Docker Desktop is not ready."
    fi
}

command="${1:-help}"
case "${command}" in
    doctor)
        doctor
        ;;
    up)
        if [[ -n "${2:-}" && "${2}" != "--build" ]]; then
            fail "Unknown up option '${2}'. Use --build or no option."
        fi
        start_preview "${2:-}"
        ;;
    status)
        require_ready_docker
        compose ps
        ;;
    logs)
        require_ready_docker
        if [[ "${2:-}" == "--follow" ]]; then
            compose logs --tail=200 --follow "${SERVICE_NAME}"
        elif [[ -n "${2:-}" ]]; then
            fail "Unknown logs option '${2}'. Use --follow or no option."
        else
            compose logs --tail=200 "${SERVICE_NAME}"
        fi
        ;;
    down)
        require_ready_docker
        compose down
        pass "Preview stopped. Named volumes and uploaded data were preserved."
        ;;
    url)
        validate_web_port
        printf 'http://127.0.0.1:%s\n' "${WEB_PORT}"
        ;;
    help|-h|--help)
        usage
        ;;
    *)
        usage >&2
        fail "Unknown command '${command}'."
        ;;
esac
