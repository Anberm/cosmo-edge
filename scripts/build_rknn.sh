#!/bin/bash
set -euo pipefail
export LC_ALL=C.UTF-8

RESOURCE_DIR=""
RKNN_ROOT_PATH="${RKNN_ROOT:-}"
ROCKCHIP_MEDIA_ROOT_PATH="${ROCKCHIP_MEDIA_ROOT:-}"
DEV_MODE=OFF
BUILD_TESTS_FLAG=OFF
while getopts "m:r:p:tT" opt; do
    case ${opt} in
        m) RESOURCE_DIR="${OPTARG}" ;;
        r) RKNN_ROOT_PATH="${OPTARG}" ;;
        p) ROCKCHIP_MEDIA_ROOT_PATH="${OPTARG}" ;;
        t) DEV_MODE=ON ;;
        T) BUILD_TESTS_FLAG=ON ;;
        *) echo "Usage: $0 -r <rknn-runtime-root> [-p <rockchip-media-root>] [-m <resource-dir>] [-t] [-T]"; exit 1 ;;
    esac
done

if [ -z "${PROJECT_ROOT_PATH:-}" ]; then
    PROJECT_ROOT_PATH=$(cd "$(dirname "$0")/.." && pwd)
fi

MEDIA_CPU_BACKEND=ON
MEDIA_ROCKCHIP_BACKEND=OFF
if [ -n "${ROCKCHIP_MEDIA_ROOT_PATH}" ]; then
    MEDIA_CPU_BACKEND=OFF
    MEDIA_ROCKCHIP_BACKEND=ON
fi
if [ -z "${RKNN_ROOT_PATH}" ]; then
    echo "ERROR: pass -r <path> or set RKNN_ROOT" >&2
    exit 1
fi
if [ -z "${RESOURCE_DIR}" ]; then
    RESOURCE_DIR="${PROJECT_ROOT_PATH}/data/resource/aiboxresource_x86"
elif [ "${RESOURCE_DIR#/}" = "${RESOURCE_DIR}" ]; then
    RESOURCE_DIR="${PROJECT_ROOT_PATH}/${RESOURCE_DIR}"
fi

RESOURCE_MODELS_DIR="${PROJECT_ROOT_PATH}/data/resource/aiboxresource_rknn/models"
RESOURCE_OVERLAY_DIR="${PROJECT_ROOT_PATH}/data/resource/aiboxresource_rknn"
BUILD_DIR="${PROJECT_ROOT_PATH}/build_rknn"
INSTALL_DIR="${BUILD_DIR}/install"
mkdir -p "${BUILD_DIR}"
rm -rf "${INSTALL_DIR}"

cmake -S "${PROJECT_ROOT_PATH}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" \
    -DCOSMO_TARGET_ARCH=aarch64 \
    -DCOSMO_NN_USE_SOPHON_BACKEND=OFF \
    -DCOSMO_NN_USE_CPU_BACKEND=OFF \
    -DCOSMO_NN_USE_RKNN_BACKEND=ON \
    -DCOSMO_MEDIA_USE_SOPHON_BACKEND=OFF \
    -DCOSMO_MEDIA_USE_CPU_BACKEND="${MEDIA_CPU_BACKEND}" \
    -DCOSMO_MEDIA_USE_ROCKCHIP_BACKEND="${MEDIA_ROCKCHIP_BACKEND}" \
    -DCOSMO_RKNN_ROOT="${RKNN_ROOT_PATH}" \
    -DCOSMO_ROCKCHIP_MEDIA_ROOT="${ROCKCHIP_MEDIA_ROOT_PATH}" \
    -DCOSMO_DEV_MODE="${DEV_MODE}" \
    -DBUILD_TESTS="${BUILD_TESTS_FLAG}" \
    -DRESOURCE_DIR="${RESOURCE_DIR}" \
    -DRESOURCE_OVERLAY_DIR="${RESOURCE_OVERLAY_DIR}" \
    -DRESOURCE_MODELS_DIR="${RESOURCE_MODELS_DIR}"

ln -sf "${BUILD_DIR}/compile_commands.json" "${PROJECT_ROOT_PATH}/compile_commands.json" 2>/dev/null || true
cmake --build "${BUILD_DIR}" --target install -j"$(nproc)"
if [ "${BUILD_TESTS_FLAG}" = "ON" ]; then
    cmake --build "${BUILD_DIR}" --target cosmo-tests -j"$(nproc)"
fi
cmake --build "${BUILD_DIR}" --target package_all
