#!/usr/bin/env python3
"""Extract YOLOv8 box/class heads so DFL and sigmoid stay on the host."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import onnx


OUTPUT_NAMES = [
    f"/model.22/{kind}.{branch}/{kind}.{branch}.2/Conv_output_0"
    for branch in range(3)
    for kind in ("cv2", "cv3")
]


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()
    source = args.input.resolve()
    output = args.output.resolve()
    report = (args.report or output.with_suffix(output.suffix + ".provenance.json")).resolve()
    if not source.is_file():
        parser.error(f"input does not exist: {source}")
    for candidate in (output, report):
        if candidate.exists() and not args.force:
            parser.error(f"refusing to overwrite {candidate}; pass --force")

    inferred = onnx.shape_inference.infer_shapes(onnx.load(source))
    known_values = {
        item.name: item for item in list(inferred.graph.value_info) + list(inferred.graph.output)
    }
    missing = [name for name in OUTPUT_NAMES if name not in known_values]
    if missing:
        raise RuntimeError(f"source is not the expected YOLOv8 graph; missing outputs: {missing}")
    extractor = onnx.utils.Extractor(inferred)
    extracted = extractor.extract_model(["images"], OUTPUT_NAMES)
    onnx.checker.check_model(extracted)
    output.parent.mkdir(parents=True, exist_ok=True)
    onnx.save(extracted, output)
    checked = onnx.load(output)
    onnx.checker.check_model(checked)
    output_shapes = [
        [dimension.dim_value for dimension in item.type.tensor_type.shape.dim]
        for item in checked.graph.output
    ]
    provenance = {
        "schema_version": 1,
        "tool": "onnx.utils.Extractor",
        "onnx_version": onnx.__version__,
        "source": {"path": str(source), "sha256": sha256(source)},
        "extracted": {
            "path": str(output),
            "sha256": sha256(output),
            "outputs": [
                {"name": name, "shape": shape} for name, shape in zip(OUTPUT_NAMES, output_shapes)
            ],
        },
        "host_output_adapter": "yolo_dfl_6head_v1",
    }
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(json.dumps(provenance, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(provenance, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
