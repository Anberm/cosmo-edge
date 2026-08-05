#!/usr/bin/env python3
"""Convert an ONNX model to the RKNN Toolkit2 supported opset and record provenance."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import onnx


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
    parser.add_argument("--opset", required=True, type=int)
    parser.add_argument("--ir-version", type=int)
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

    model = onnx.load(source)
    source_opsets = {item.domain: item.version for item in model.opset_import}
    source_ir = model.ir_version
    converted = onnx.version_converter.convert_version(model, args.opset)
    if args.ir_version is not None:
        converted.ir_version = args.ir_version
    onnx.checker.check_model(converted)

    output.parent.mkdir(parents=True, exist_ok=True)
    onnx.save(converted, output)
    converted_check = onnx.load(output)
    onnx.checker.check_model(converted_check)
    converted_opsets = {item.domain: item.version for item in converted_check.opset_import}

    provenance = {
        "schema_version": 1,
        "tool": "onnx.version_converter",
        "onnx_version": onnx.__version__,
        "source": {
            "path": str(source),
            "sha256": sha256(source),
            "ir_version": source_ir,
            "opsets": source_opsets,
        },
        "converted": {
            "path": str(output),
            "sha256": sha256(output),
            "ir_version": converted_check.ir_version,
            "opsets": converted_opsets,
        },
    }
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(json.dumps(provenance, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(provenance, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
