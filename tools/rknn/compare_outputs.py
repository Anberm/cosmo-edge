#!/usr/bin/env python3
"""Compare board RKNN float outputs with an ONNX Runtime reference."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np


def load_values(path: Path, shape: list[int]) -> np.ndarray:
    if path.suffix == ".npy":
        values = np.load(path)
    else:
        values = np.fromfile(path, dtype=np.float32)
    expected = int(np.prod(shape))
    if values.size != expected:
        raise ValueError(f"{path} has {values.size} values; expected {expected} for shape {shape}")
    return values.astype(np.float32, copy=False).reshape(shape)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--spec", required=True, type=Path)
    parser.add_argument("--precision", required=True, choices=("fp16", "int8"))
    parser.add_argument("--reference", required=True, type=Path)
    parser.add_argument("--actual", required=True, type=Path)
    parser.add_argument("--output-index", type=int, default=0)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()

    spec = json.loads(args.spec.read_text(encoding="utf-8"))
    shape = spec["outputs"][args.output_index]["shape"]
    reference = load_values(args.reference, shape)
    actual = load_values(args.actual, shape)
    difference = actual - reference
    ref_flat = reference.ravel().astype(np.float64)
    actual_flat = actual.ravel().astype(np.float64)
    denominator = np.linalg.norm(ref_flat) * np.linalg.norm(actual_flat)
    cosine = float(np.dot(ref_flat, actual_flat) / denominator) if denominator else float(np.array_equal(reference, actual))
    maximum_absolute_error = float(np.max(np.abs(difference)))
    metrics = {
        "maximum_absolute_error": maximum_absolute_error,
        "mean_absolute_error": float(np.mean(np.abs(difference))),
        "root_mean_square_error": float(np.sqrt(np.mean(np.square(difference)))),
        "cosine_similarity": cosine,
    }
    if spec["model_type"] == "classify":
        metrics["reference_argmax"] = int(np.argmax(reference))
        metrics["actual_argmax"] = int(np.argmax(actual))
        metrics["argmax_match"] = metrics["reference_argmax"] == metrics["actual_argmax"]
    limits = spec["validation"][args.precision]
    passed = cosine >= limits["minimum_cosine_similarity"] and maximum_absolute_error <= limits["maximum_absolute_error"]
    if spec["model_type"] == "classify":
        passed = passed and metrics["argmax_match"]
    result = {"passed": passed, "precision": args.precision, "metrics": metrics, "limits": limits}
    serialized = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(serialized, encoding="utf-8")
    print(serialized, end="")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
