#!/usr/bin/env python3
"""Verify the shared open Sophon models bound to BM1688 and CV186X evidence."""

from __future__ import annotations

import hashlib
import json
import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]
MODEL_ROOT = ROOT / "data" / "resource" / "aiboxresource" / "models"
IDENTITY_ROOT = ROOT / "docs" / "benchmarks" / "scenario-bench" / "v1.1" / "models"
EXPECTED = {
    "prod_BM1688_6047042_YOLOV8n_V1.0.0": {
        "public_id": "person-detector",
        "sha256": "56b207ef2876da76505e403a049d3c44a411b9fe707ab73dc64f1cd9d9b6c5c8",
        "size_bytes": 7_023_600,
        "model_type": "yolov8_det",
        "algorithm_code": "6047042",
        "input_size": [640, 640],
    },
    "prod_BM1688_7486163_helmet_V1.0.0": {
        "public_id": "safety-helmet-classifier",
        "sha256": "33b0fb4bcb29e41a92f9c1c518671aefc69cbf9207934deaba32ca7cd8cd7c8a",
        "size_bytes": 6_001_416,
        "model_type": "classify",
        "algorithm_code": "7486163",
        "input_size": [224, 224],
    },
}


def fail(message: str) -> None:
    raise SystemExit(f"Sophon open-model verification failed: {message}")


identities = {
    platform: json.loads((IDENTITY_ROOT / f"{platform}.json").read_text(encoding="utf-8"))["models"]
    for platform in ("bm1688", "cv186x")
}

for directory, expected in EXPECTED.items():
    root = MODEL_ROOT / directory
    model = root / "model.nn"
    config_path = root / "config.json"
    for path in (model, config_path):
        if not path.is_file():
            fail(f"missing {path.relative_to(ROOT)}")

    model_bytes = model.read_bytes()
    if not model_bytes.startswith(b"CENN"):
        fail(f"{model.relative_to(ROOT)} is not a plaintext Open artifact")
    if len(model_bytes) != expected["size_bytes"]:
        fail(f"size mismatch for {model.relative_to(ROOT)}")
    actual_sha256 = hashlib.sha256(model_bytes).hexdigest()
    if actual_sha256 != expected["sha256"]:
        fail(f"SHA-256 mismatch for {model.relative_to(ROOT)}")

    config = json.loads(config_path.read_text(encoding="utf-8"))
    if config.get("model_type") != expected["model_type"]:
        fail(f"model_type mismatch in {directory}")
    if config.get("algorithm_code") != expected["algorithm_code"]:
        fail(f"algorithm_code mismatch in {directory}")
    models = config.get("models") or []
    if len(models) != 1 or models[0].get("params", {}).get("input_size") != expected["input_size"]:
        fail(f"input_size mismatch in {directory}")

    repository_path = model.parent.relative_to(ROOT).as_posix()
    for platform, entries in identities.items():
        identity = next(
            (entry for entry in entries if entry.get("publicId") == expected["public_id"]),
            None,
        )
        if identity is None:
            fail(f"{platform} identity is missing for {expected['public_id']}")
        if identity.get("sha256") != actual_sha256:
            fail(f"{platform} identity hash mismatch for {expected['public_id']}")
        if identity.get("sizeBytes") != len(model_bytes):
            fail(f"{platform} identity size mismatch for {expected['public_id']}")
        if identity.get("repositoryPath") != repository_path:
            fail(f"{platform} repository path mismatch for {expected['public_id']}")

print("Verified 2 shared Open Sophon models and byte-identical BM1688/CV186X evidence bindings.")
