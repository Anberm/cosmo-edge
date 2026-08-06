#!/usr/bin/env python3
"""Reconstruct the standard [1,84,8400] YOLOv8 tensor from six RKNN heads."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_head(path: Path, shape: list[int]) -> np.ndarray:
    values = np.load(path) if path.suffix == ".npy" else np.fromfile(path, dtype=np.float32)
    if values.size != int(np.prod(shape)):
        raise ValueError(f"{path} has {values.size} values, expected {int(np.prod(shape))}")
    return values.astype(np.float32, copy=False).reshape(shape)


def sigmoid(values: np.ndarray) -> np.ndarray:
    positive = values >= 0
    result = np.empty_like(values, dtype=np.float32)
    result[positive] = 1.0 / (1.0 + np.exp(-values[positive]))
    exponential = np.exp(values[~positive])
    result[~positive] = exponential / (1.0 + exponential)
    return result


def reconstruct(heads: list[np.ndarray], input_size: int) -> np.ndarray:
    if len(heads) != 6:
        raise ValueError("yolo_dfl_6head_v1 requires three box/class head pairs")
    branches = []
    weights = np.arange(16, dtype=np.float32).reshape(1, 1, 16, 1, 1)
    for branch in range(3):
        box_logits = heads[branch * 2]
        class_logits = heads[branch * 2 + 1]
        batch, channels, height, width = box_logits.shape
        if channels != 64 or class_logits.shape != (batch, 80, height, width):
            raise ValueError("unexpected YOLOv8 head shape")
        distributions = box_logits.reshape(batch, 4, 16, height, width)
        distributions = distributions - np.max(distributions, axis=2, keepdims=True)
        distributions = np.exp(distributions)
        distributions /= np.sum(distributions, axis=2, keepdims=True)
        distances = np.sum(distributions * weights, axis=2)
        rows, columns = np.meshgrid(np.arange(height), np.arange(width), indexing="ij")
        grid = np.stack([columns, rows], axis=0).reshape(1, 2, height, width) + 0.5
        upper_left = grid - distances[:, :2]
        lower_right = grid + distances[:, 2:]
        xy = (upper_left + lower_right) / 2.0
        wh = lower_right - upper_left
        coordinates = np.concatenate([xy, wh], axis=1) * (input_size // height)
        probabilities = sigmoid(class_logits)
        branches.append(
            np.concatenate(
                [coordinates.reshape(batch, 4, -1), probabilities.reshape(batch, 80, -1)],
                axis=1,
            )
        )
    return np.ascontiguousarray(np.concatenate(branches, axis=2), dtype=np.float32)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--spec", required=True, type=Path)
    parser.add_argument("--input-dir", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()

    spec = json.loads(args.spec.read_text(encoding="utf-8"))
    if spec["conversion"].get("output_adapter") != "yolo_dfl_6head_v1":
        raise ValueError("spec does not select yolo_dfl_6head_v1")
    runtime_outputs = spec["runtime_outputs"]
    input_paths = [args.input_dir / f"output-{index}.f32.bin" for index in range(len(runtime_outputs))]
    heads = [load_head(path, item["shape"]) for path, item in zip(input_paths, runtime_outputs)]
    logical = reconstruct(heads, int(spec["input"]["shape"][2]))
    expected_shape = spec["outputs"][0]["shape"]
    if list(logical.shape) != expected_shape:
        raise ValueError(f"logical output shape {list(logical.shape)} does not match {expected_shape}")

    output_path = args.output.resolve()
    if output_path.suffix != ".npy":
        parser.error("--output must end in .npy")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    np.save(output_path, logical)
    binary_path = output_path.with_suffix(".f32.bin")
    logical.tofile(binary_path)
    report = {
        "schema_version": 1,
        "adapter": "yolo_dfl_6head_v1",
        "inputs": [
            {"path": str(path.resolve()), "sha256": sha256(path.resolve()), "shape": item["shape"]}
            for path, item in zip(input_paths, runtime_outputs)
        ],
        "output": {
            "path": str(output_path),
            "sha256": sha256(output_path),
            "binary_path": str(binary_path),
            "binary_sha256": sha256(binary_path),
            "shape": list(logical.shape),
        },
    }
    report_path = (args.report or output_path.with_suffix(".reconstruction.json")).resolve()
    report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
