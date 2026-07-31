#!/bin/bash
set -euo pipefail

export LC_ALL=C.UTF-8

COSMO_MODEL_GUARD_BUILD_PROFILE="${COSMO_MODEL_GUARD_BUILD_PROFILE:-public-runtime}"
case "${COSMO_MODEL_GUARD_BUILD_PROFILE}" in
    public-runtime)
        PACKAGE_VARIANT="SOURCE"
        ;;
    production-release)
        PACKAGE_VARIANT="production-release"
        ;;
    *)
        echo "ERROR: COSMO_MODEL_GUARD_BUILD_PROFILE must be public-runtime or production-release" >&2
        exit 1
        ;;
esac

# ── Parse options ──
# -t = dev mode (disable watchdog); -T = also build cosmo-tests in this pass.
RESOURCE_DIR=""
DEV_MODE=OFF
BUILD_TESTS_FLAG=OFF
while getopts "m:tT" opt; do
    case $opt in
        m) RESOURCE_DIR="$OPTARG" ;;
        t) DEV_MODE=ON ;;
        T) BUILD_TESTS_FLAG=ON ;;
        *) echo "Usage: $0 [-m <resource_repo_path>] [-t (enable dev mode)] [-T (also build cosmo-tests)]"; exit 1 ;;
    esac
done

if [ -z "${PROJECT_ROOT_PATH:-}" ]; then
    PROJECT_ROOT_PATH="$(cd "$(dirname "$0")/.." && pwd -P)"
fi

if [ "${COSMO_MODEL_GUARD_BUILD_PROFILE}" = "public-runtime" ]; then
    COSMO_EDGE_SOURCE_COMMIT="${COSMO_EDGE_SOURCE_COMMIT:-}"
    if [ -z "${COSMO_EDGE_SOURCE_COMMIT}" ]; then
        if ! command -v git >/dev/null 2>&1; then
            echo "ERROR: SOURCE packaging requires Git or an explicit COSMO_EDGE_SOURCE_COMMIT" >&2
            exit 1
        fi
        if ! COSMO_EDGE_SOURCE_COMMIT="$(
            git -c safe.directory="${PROJECT_ROOT_PATH}" \
                -C "${PROJECT_ROOT_PATH}" rev-parse --verify 'HEAD^{commit}'
        )"; then
            echo "ERROR: cannot resolve the Edge commit; set COSMO_EDGE_SOURCE_COMMIT explicitly" >&2
            exit 1
        fi
    fi
    if [ "${#COSMO_EDGE_SOURCE_COMMIT}" -ne 40 ]; then
        echo "ERROR: COSMO_EDGE_SOURCE_COMMIT must be lower-case 40-hex" >&2
        exit 1
    fi
    case "${COSMO_EDGE_SOURCE_COMMIT}" in
        *[!0-9a-f]*)
            echo "ERROR: COSMO_EDGE_SOURCE_COMMIT must be lower-case 40-hex" >&2
            exit 1
            ;;
        *) ;;
    esac
    export COSMO_EDGE_SOURCE_COMMIT
fi

if [ -z "${RESOURCE_DIR}" ]; then
    RESOURCE_DIR="${PROJECT_ROOT_PATH}/data/resource/aiboxresource"
elif [ "${RESOURCE_DIR#/}" = "${RESOURCE_DIR}" ]; then
    RESOURCE_DIR="${PROJECT_ROOT_PATH}/${RESOURCE_DIR}"
fi

if [ ! -d "${RESOURCE_DIR}" ]; then
    echo "ERROR: Resource directory not found: ${RESOURCE_DIR}" >&2
    exit 1
fi

BUILD_DIR="${PROJECT_ROOT_PATH}/build"
INSTALL_DIR="${BUILD_DIR}/install"
PACKAGE_DIR="${BUILD_DIR}/packages"
COSMO_GUARD_SDK_DIR="${COSMO_MODEL_GUARD_SDK_ROOT:-${PROJECT_ROOT_PATH}/prebuild/model-guard-v2}"
MODEL_GUARD_PROFILE_ARGS=(
    -DCOSMO_MODEL_GUARD_BUILD_PROFILE="${COSMO_MODEL_GUARD_BUILD_PROFILE}"
    -DCOSMO_EDGE_SOURCE_COMMIT="${COSMO_EDGE_SOURCE_COMMIT:-}"
)
RELEASE_BOOTSTRAP_ARGS=(
    -DCOSMO_REQUIRE_RELEASE_BOOTSTRAP="${COSMO_REQUIRE_RELEASE_BOOTSTRAP:-OFF}"
    -DCOSMO_RELEASE_PUBLIC_KEY_OBJECT="${COSMO_RELEASE_PUBLIC_KEY_OBJECT:-}"
)
if [ -d "${INSTALL_DIR}" ]; then
    rm -rf -- "${INSTALL_DIR}"
fi

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

echo "Dev mode: ${DEV_MODE}"
echo "Resource dir: ${RESOURCE_DIR}"
echo "Package variant: ${PACKAGE_VARIANT}"
echo "Internal Model Guard build profile: ${COSMO_MODEL_GUARD_BUILD_PROFILE}"
if [ "${COSMO_MODEL_GUARD_BUILD_PROFILE}" = "public-runtime" ]; then
    echo "Edge source commit: ${COSMO_EDGE_SOURCE_COMMIT}"
fi
echo "Configuring protected build..."
cmake   -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" \
        -DBUILD_TESTS="${BUILD_TESTS_FLAG}" \
        -DCOSMO_DEV_MODE="${DEV_MODE}" \
        -DCOSMO_MODEL_GUARD_SDK_ROOT="${COSMO_GUARD_SDK_DIR}" \
        -DRESOURCE_DIR="${RESOURCE_DIR}" \
        "${MODEL_GUARD_PROFILE_ARGS[@]}" \
        "${RELEASE_BOOTSTRAP_ARGS[@]}" \
        ..

# Symlink compile_commands.json to project root for IDE and static analysis tools
ln -sf "${BUILD_DIR}/compile_commands.json" "${PROJECT_ROOT_PATH}/compile_commands.json" 2>/dev/null || true

echo "Building Cosmo ..."
build_targets=(--target install)
if [ "${BUILD_TESTS_FLAG}" = "ON" ]; then
    echo "Also building cosmo-tests in this pass..."
    build_targets+=(
        --target cosmo-tests
        --target cosmo-release-bootstrap-test-fixture
        --target cosmo-release-bootstrap-verifier-tests
    )
fi
cmake --build . "${build_targets[@]}" -j"$(nproc)"

echo "Auditing installed AArch64 ELF paths..."
unsafe_elf_path=0
while IFS= read -r -d '' installed_file; do
    if aarch64-linux-gnu-readelf -hW "${installed_file}" >/dev/null 2>&1; then
        dynamic_metadata=$(aarch64-linux-gnu-readelf -dW "${installed_file}")
        if grep -Eq '/workspace|thirdparty_install|3rd/libsophon' \
                <<<"${dynamic_metadata}"
        then
            echo "ERROR: installed ELF dynamic metadata leaks a build-only path: ${installed_file}" >&2
            unsafe_elf_path=1
        fi
    fi
done < <(find "${INSTALL_DIR}" -type f -print0)
if [ "${unsafe_elf_path}" -ne 0 ]; then
    exit 1
fi

if [ "${BUILD_TESTS_FLAG}" = "ON" ]; then
    echo "Auditing isolated AArch64 release-bootstrap test images..."
    for bootstrap_test_image in \
        "${BUILD_DIR}/cosmo-release-bootstrap-test-fixture" \
        "${BUILD_DIR}/cosmo-release-bootstrap-verifier-tests"
    do
        test -f "${bootstrap_test_image}"
        aarch64-linux-gnu-readelf -hW "${bootstrap_test_image}" |
            grep -Eq 'Machine:[[:space:]]+AArch64$'
        bootstrap_dynamic=$(aarch64-linux-gnu-readelf -dW "${bootstrap_test_image}")
        grep -Fq '(RUNPATH)' <<<"${bootstrap_dynamic}"
        grep -Fq '[$ORIGIN/../lib]' <<<"${bootstrap_dynamic}"
        grep -Fq 'Shared library: [libcrypto.so.3]' <<<"${bootstrap_dynamic}"
        if grep -Fq '(RPATH)' <<<"${bootstrap_dynamic}" ||
           grep -Fq '/workspace' <<<"${bootstrap_dynamic}"; then
            echo "ERROR: release-bootstrap test image has an unsafe dynamic path: ${bootstrap_test_image}" >&2
            exit 1
        fi
    done
    test ! -e "${INSTALL_DIR}/bin/cosmo-release-bootstrap-test-fixture"
    test ! -e "${INSTALL_DIR}/bin/cosmo-release-bootstrap-verifier-tests"

    echo "Running protected-build security regression suites..."
    /usr/bin/python3 -I -B "${PROJECT_ROOT_PATH}/test/test_package_profile.py"
    /usr/bin/python3 -I -B "${PROJECT_ROOT_PATH}/test/test_verify_model_guard_v2_sdk.py"
    /usr/bin/python3 -I -B "${PROJECT_ROOT_PATH}/test/test_release_health_check.py"
    /usr/bin/python3 -I -B "${PROJECT_ROOT_PATH}/test/test_release_updater.py"
fi

installed_python_cache="$(
    find "${INSTALL_DIR}" \
        \( -name __pycache__ -o -name '*.pyc' -o -name '*.pyo' \) \
        -print -quit
)"
if [ -n "${installed_python_cache}" ]; then
    printf 'ERROR: installed payload contains Python bytecode cache: %q\n' \
        "${installed_python_cache}" >&2
    exit 1
fi

if [ "${COSMO_MODEL_GUARD_BUILD_PROFILE}" = "production-release" ]; then
    echo "Validating blank-device factory base..."
    for facade in bin files font lib resource scripts web; do
        test -d "${INSTALL_DIR}/${facade}"
        test ! -L "${INSTALL_DIR}/${facade}"
    done
    test ! -e "${INSTALL_DIR}/current"
    test ! -e "${INSTALL_DIR}/.releases"
    test ! -e "${INSTALL_DIR}/.release-state"

    factory_service="${INSTALL_DIR}/share/cosmo-factory/cosmo.service"
    test -f "${factory_service}"
    test ! -L "${factory_service}"
    grep -Fxq 'User=root' "${factory_service}"
    grep -Fxq \
        'WorkingDirectory=/appfs/cosmo_wander/cwai_data' \
        "${factory_service}"
    grep -Fxq \
        'ExecStart=/appfs/cosmo_wander/cwai_data/scripts/inte_run_start.sh' \
        "${factory_service}"
    if grep -Eiq 'model[- ]guard|RequiresMountsFor' "${factory_service}"; then
        echo "ERROR: factory service contains a forbidden state/mount dependency" >&2
        exit 1
    fi
fi

echo "Packaging..."
cmake --build . --target package_all

shopt -s nullglob
package_artifacts=("${PACKAGE_DIR}"/*.tar.gz)
shopt -u nullglob
if [ "${#package_artifacts[@]}" -ne 1 ] ||
   [ ! -f "${package_artifacts[0]:-}" ] ||
   [ -L "${package_artifacts[0]:-}" ]; then
    echo "ERROR: packaging must produce exactly one regular archive" >&2
    exit 1
fi

/usr/bin/python3 -I -B \
    "${PROJECT_ROOT_PATH}/scripts/verify_package_contents.py" \
    --archive "${package_artifacts[0]}" \
    --build-profile "${COSMO_MODEL_GUARD_BUILD_PROFILE}"

package_sha256="$(sha256sum -- "${package_artifacts[0]}")"
package_sha256="${package_sha256%% *}"
echo "Verified ${PACKAGE_VARIANT} package: ${package_artifacts[0]}"
echo "Package SHA-256: ${package_sha256}"
