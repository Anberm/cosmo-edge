#!/usr/bin/env python3
"""Run one ONNX reference input and persist outputs plus runtime provenance."""

from __future__ import annotations

import argparse
import hashlib
import json
import time
from pathlib import Path

import numpy as np
import onnxruntime as ort


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", required=True, type=Path)
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--iterations", type=int, default=5)
    parser.add_argument("--warmup", type=int, default=1)
    args = parser.parse_args()
    if args.iterations <= 0 or args.warmup < 0:
        parser.error("--iterations must be positive and --warmup non-negative")

    model_path = args.model.resolve()
    input_path = args.input.resolve()
    output_dir = args.output_dir.resolve()
    values = np.load(input_path).astype(np.float32, copy=False)
    session = ort.InferenceSession(str(model_path), providers=["CPUExecutionProvider"])
    model_input = session.get_inputs()[0]
    feeds = {model_input.name: values}
    for _ in range(args.warmup):
        session.run(None, feeds)
    timings = []
    outputs = None
    for _ in range(args.iterations):
        start = time.perf_counter()
        outputs = session.run(None, feeds)
        timings.append((time.perf_counter() - start) * 1000.0)
    assert outputs is not None

    output_dir.mkdir(parents=True, exist_ok=True)
    output_records = []
    for index, output in enumerate(outputs):
        array = np.asarray(output, dtype=np.float32)
        npy_path = output_dir / f"output-{index}.npy"
        bin_path = output_dir / f"output-{index}.f32.bin"
        np.save(npy_path, array)
        array.tofile(bin_path)
        output_records.append(
            {
                "index": index,
                "name": session.get_outputs()[index].name,
                "shape": list(array.shape),
                "npy": str(npy_path),
                "npy_sha256": sha256(npy_path),
                "bin": str(bin_path),
                "bin_sha256": sha256(bin_path),
            }
        )
    report = {
        "schema_version": 1,
        "onnxruntime_version": ort.__version__,
        "providers": session.get_providers(),
        "model": {"path": str(model_path), "sha256": sha256(model_path)},
        "input": {
            "path": str(input_path),
            "sha256": sha256(input_path),
            "name": model_input.name,
            "shape": list(values.shape),
            "dtype": str(values.dtype),
        },
        "outputs": output_records,
        "timing_ms": {
            "iterations": args.iterations,
            "warmup": args.warmup,
            "mean": float(np.mean(timings)),
            "minimum": float(np.min(timings)),
            "maximum": float(np.max(timings)),
        },
    }
    report_path = output_dir / "report.json"
    report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
