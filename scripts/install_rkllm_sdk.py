#!/usr/bin/env python3
"""Install the pinned Rockchip RKLLM runtime used by the RK3576 builder."""

from __future__ import annotations

import argparse
import hashlib
import os
import pathlib
import time
import urllib.error
import urllib.request


VERSION = "1.3.0"
COMMIT = "878f9361fd3afa7e167b7079918918f78d2c1c2a"
SOURCE_BASE = f"https://raw.githubusercontent.com/airockchip/rknn-llm/{COMMIT}"
FILES = {
    "include/rkllm.h": (
        "rkllm-runtime/Linux/librkllm_api/include/rkllm.h",
        "80596a578f7f8e70df6eda1c2cbead3bfced14623a190258f2bd009a3d1f72cf",
        0o644,
    ),
    "lib/librkllmrt.so": (
        "rkllm-runtime/Linux/librkllm_api/aarch64/librkllmrt.so",
        "6a9e4fc5324c68921c3a900340361e107af7599fe34dc8fa7759b2c5ae22a6e6",
        0o755,
    ),
    "LICENSE": (
        "LICENSE",
        "8d670a646eb8cf28fb7c63a5c9126c224a3a3f8124b00a3b9184df9a2ed298b8",
        0o644,
    ),
}


def download(url: str) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": "cosmo-edge-rk3576-builder"})
    last_error: Exception | None = None
    for attempt in range(1, 4):
        try:
            with urllib.request.urlopen(request, timeout=120) as response:
                return response.read()
        except (OSError, urllib.error.URLError) as error:
            last_error = error
            if attempt < 3:
                time.sleep(attempt * 2)
    raise RuntimeError(f"download failed after 3 attempts: {url}: {last_error}")


def install(root: pathlib.Path) -> None:
    root.mkdir(parents=True, exist_ok=True)
    for destination_name, (source_name, expected_sha256, mode) in FILES.items():
        url = f"{SOURCE_BASE}/{source_name}"
        data = download(url)
        actual_sha256 = hashlib.sha256(data).hexdigest()
        if actual_sha256 != expected_sha256:
            raise RuntimeError(
                f"SHA-256 mismatch for {source_name}: expected {expected_sha256}, got {actual_sha256}"
            )
        destination = root / destination_name
        destination.parent.mkdir(parents=True, exist_ok=True)
        temporary = destination.with_name(f".{destination.name}.tmp")
        temporary.write_bytes(data)
        os.chmod(temporary, mode)
        os.replace(temporary, destination)
        print(f"Installed {destination_name} ({len(data)} bytes, sha256={actual_sha256})")

    (root / "VERSION").write_text(
        f"RKLLM Runtime v{VERSION}\nsource_commit={COMMIT}\n", encoding="utf-8"
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True, type=pathlib.Path)
    arguments = parser.parse_args()
    install(arguments.root.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
