#!/usr/bin/env bash

set -euo pipefail

usage() {
    echo "Usage: $0 <offline-bundle-dir> [venv-dir]" >&2
}

if [[ $# -lt 1 || $# -gt 2 ]]; then
    usage
    exit 2
fi

bundle_dir="$(cd "$1" && pwd)"
venv_dir="${2:-$PWD/.venv-rknn-2.3.2}"
wheels_dir="$bundle_dir/wheels-cp310-linux-x86_64"
toolkit_wheel="$bundle_dir/toolkit/rknn_toolkit2-2.3.2-cp310-cp310-manylinux_2_17_x86_64.manylinux2014_x86_64.whl"

if [[ "$(uname -m)" != "x86_64" ]]; then
    echo "RKNN conversion environment requires an x86_64 Linux host" >&2
    exit 1
fi

if [[ "$(python3 -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")')" != "3.10" ]]; then
    echo "Python 3.10 is required by this locked environment" >&2
    exit 1
fi

for required in "$wheels_dir" "$toolkit_wheel" "$bundle_dir/SHA256SUMS"; do
    if [[ ! -e "$required" ]]; then
        echo "Missing offline artifact: $required" >&2
        exit 1
    fi
done

(
    cd "$bundle_dir"
    sha256sum --check SHA256SUMS
)

python3 -m venv "$venv_dir"
# shellcheck disable=SC1091
source "$venv_dir/bin/activate"

python -m pip install --no-index --find-links "$wheels_dir" \
    "$wheels_dir/torch-2.0.1+cpu-cp310-cp310-linux_x86_64.whl"
python -m pip install --no-index --find-links "$wheels_dir" "$toolkit_wheel"

python - <<'PY'
import numpy
import onnx
import onnxruntime
import torch
from rknn.api import RKNN

print("RKNN environment ready")
print("numpy", numpy.__version__)
print("onnx", onnx.__version__)
print("onnxruntime", onnxruntime.__version__)
print("torch", torch.__version__)
print("rknn", RKNN)
PY
