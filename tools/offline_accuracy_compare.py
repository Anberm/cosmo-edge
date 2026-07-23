#!/usr/bin/env python3

"""Compare versioned reference and candidate business outputs frame by frame."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import statistics
import sys
from pathlib import Path
from typing import Any


SCHEMA_VERSION = 1
DEFAULT_GATES = {
    "detector_box_iou_median_min": 0.95,
    "detector_box_iou_p5_min": 0.90,
    "detector_class_consistency_min": 0.99,
    "detector_confidence_abs_error_p99_max": 0.05,
    "detector_match_iou_min": 0.50,
    "detector_nms_difference_rate_max": 0.01,
    "detector_unmatched_rate_max": 0.01,
    "minimum_matched_detections": 1,
    "classification_probability_abs_error_p99_max": 0.02,
    "classification_top1_consistency_min": 1.0,
}


class AcceptanceError(Exception):
    """Invalid manifest, artifact, or result data."""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def percentile(values: list[float], quantile: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    position = (len(ordered) - 1) * quantile
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return ordered[lower]
    fraction = position - lower
    return ordered[lower] * (1.0 - fraction) + ordered[upper] * fraction


def require_number(value: Any, label: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise AcceptanceError(f"{label} must be a finite number")
    return float(value)


def resolve_artifact(manifest_dir: Path, name: str, spec: Any) -> dict[str, Any]:
    if not isinstance(spec, dict):
        raise AcceptanceError(f"artifacts.{name} must be an object")
    path_text = spec.get("path")
    expected = spec.get("sha256")
    if not isinstance(path_text, str) or not path_text:
        raise AcceptanceError(f"artifacts.{name}.path must be a non-empty string")
    if (not isinstance(expected, str) or len(expected) != 64
            or any(not (char.isdigit() or "a" <= char <= "f") for char in expected)):
        raise AcceptanceError(f"artifacts.{name}.sha256 must be a lowercase SHA-256")
    path = Path(path_text)
    if not path.is_absolute():
        path = manifest_dir / path
    path = path.resolve()
    if not path.is_file():
        raise AcceptanceError(f"artifacts.{name} is not a readable file: {path}")
    actual = sha256_file(path)
    if actual != expected:
        raise AcceptanceError(f"artifacts.{name} SHA-256 mismatch: expected {expected}, got {actual}")
    return {"name": name, "path": str(path), "sha256": actual, "bytes": path.stat().st_size}


def load_manifest(path: Path) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise AcceptanceError(f"cannot read manifest: {error}") from error
    if not isinstance(manifest, dict) or manifest.get("schema_version") != SCHEMA_VERSION:
        raise AcceptanceError(f"manifest schema_version must be {SCHEMA_VERSION}")
    for field in ("dataset_id", "reference_backend", "candidate_backend"):
        if not isinstance(manifest.get(field), str) or not manifest[field]:
            raise AcceptanceError(f"manifest.{field} must be a non-empty string")
    workload_types = manifest.get("workload_types")
    if not isinstance(workload_types, list) or not workload_types:
        raise AcceptanceError("manifest.workload_types must contain detection and/or classification")
    if set(workload_types) - {"detection", "classification"}:
        raise AcceptanceError("manifest.workload_types supports only detection and classification")

    artifacts = manifest.get("artifacts")
    if not isinstance(artifacts, dict):
        raise AcceptanceError("manifest.artifacts must be an object")
    required = {"model", "config", "labels", "reference_results", "candidate_results"}
    missing = sorted(required - artifacts.keys())
    if missing:
        raise AcceptanceError(f"manifest.artifacts missing: {', '.join(missing)}")
    videos = artifacts.get("videos")
    if not isinstance(videos, list) or not videos:
        raise AcceptanceError("manifest.artifacts.videos must be a non-empty array")

    verified = [resolve_artifact(path.parent, name, artifacts[name]) for name in sorted(required)]
    verified.extend(resolve_artifact(path.parent, f"videos[{index}]", spec) for index, spec in enumerate(videos))

    raw_gates = manifest.get("gates", {})
    if not isinstance(raw_gates, dict):
        raise AcceptanceError("manifest.gates must be an object")
    unknown = sorted(set(raw_gates) - set(DEFAULT_GATES))
    if unknown:
        raise AcceptanceError(f"manifest.gates has unknown fields: {', '.join(unknown)}")
    gates = dict(DEFAULT_GATES)
    for key, value in raw_gates.items():
        gates[key] = require_number(value, f"gates.{key}")
    manifest["gates"] = gates
    return manifest, verified


def read_results(path: Path) -> dict[str, dict[str, Any]]:
    records: dict[str, dict[str, Any]] = {}
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise AcceptanceError(f"cannot read results {path}: {error}") from error
    for line_number, line in enumerate(lines, 1):
        if not line.strip():
            continue
        try:
            record = json.loads(line)
        except json.JSONDecodeError as error:
            raise AcceptanceError(f"{path}:{line_number}: invalid JSON: {error}") from error
        if not isinstance(record, dict):
            raise AcceptanceError(f"{path}:{line_number}: record must be an object")
        sample_id = record.get("sample_id")
        task_type = record.get("task_type")
        if not isinstance(sample_id, str) or not sample_id:
            raise AcceptanceError(f"{path}:{line_number}: sample_id must be a non-empty string")
        if sample_id in records:
            raise AcceptanceError(f"{path}:{line_number}: duplicate sample_id {sample_id}")
        if task_type not in {"detection", "classification"}:
            raise AcceptanceError(f"{path}:{line_number}: unsupported task_type {task_type}")
        if task_type == "detection":
            validate_detection_record(record, path, line_number)
        else:
            validate_classification_record(record, path, line_number)
        records[sample_id] = record
    if not records:
        raise AcceptanceError(f"results file contains no records: {path}")
    return records


def validate_detection_record(record: dict[str, Any], path: Path, line_number: int) -> None:
    detections = record.get("detections")
    if not isinstance(detections, list):
        raise AcceptanceError(f"{path}:{line_number}: detections must be an array")
    for index, detection in enumerate(detections):
        label = f"{path}:{line_number}: detections[{index}]"
        if not isinstance(detection, dict):
            raise AcceptanceError(f"{label} must be an object")
        if not isinstance(detection.get("class_id"), int) or isinstance(detection["class_id"], bool):
            raise AcceptanceError(f"{label}.class_id must be an integer")
        confidence = require_number(detection.get("confidence"), f"{label}.confidence")
        if not 0.0 <= confidence <= 1.0:
            raise AcceptanceError(f"{label}.confidence must be in [0, 1]")
        box = detection.get("box_xyxy")
        if not isinstance(box, list) or len(box) != 4:
            raise AcceptanceError(f"{label}.box_xyxy must contain four numbers")
        x1, y1, x2, y2 = [require_number(value, f"{label}.box_xyxy") for value in box]
        if x2 <= x1 or y2 <= y1:
            raise AcceptanceError(f"{label}.box_xyxy must have positive area")
    nms = record.get("nms")
    if nms is not None:
        if not isinstance(nms, dict):
            raise AcceptanceError(f"{path}:{line_number}: nms must be an object")
        for field in ("pre_count", "post_count"):
            if field in nms and (not isinstance(nms[field], int) or isinstance(nms[field], bool) or nms[field] < 0):
                raise AcceptanceError(f"{path}:{line_number}: nms.{field} must be a non-negative integer")


def validate_classification_record(record: dict[str, Any], path: Path, line_number: int) -> None:
    probabilities = record.get("probabilities")
    if not isinstance(probabilities, list) or not probabilities:
        raise AcceptanceError(f"{path}:{line_number}: probabilities must be a non-empty array")
    values = [require_number(value, f"{path}:{line_number}: probabilities") for value in probabilities]
    if any(value < 0.0 or value > 1.0 for value in values):
        raise AcceptanceError(f"{path}:{line_number}: probabilities must be in [0, 1]")
    top1 = record.get("top1", max(range(len(values)), key=values.__getitem__))
    if not isinstance(top1, int) or isinstance(top1, bool) or not 0 <= top1 < len(values):
        raise AcceptanceError(f"{path}:{line_number}: top1 must index probabilities")


def box_iou(left: list[float], right: list[float]) -> float:
    ix1, iy1 = max(left[0], right[0]), max(left[1], right[1])
    ix2, iy2 = min(left[2], right[2]), min(left[3], right[3])
    intersection = max(0.0, ix2 - ix1) * max(0.0, iy2 - iy1)
    left_area = (left[2] - left[0]) * (left[3] - left[1])
    right_area = (right[2] - right[0]) * (right[3] - right[1])
    union = left_area + right_area - intersection
    return intersection / union if union > 0.0 else 0.0


def match_detections(reference: list[dict[str, Any]], candidate: list[dict[str, Any]], minimum_iou: float):
    pairs = []
    for ref_index, ref in enumerate(reference):
        for candidate_index, actual in enumerate(candidate):
            pairs.append((box_iou(ref["box_xyxy"], actual["box_xyxy"]), ref_index, candidate_index))
    used_reference: set[int] = set()
    used_candidate: set[int] = set()
    matches = []
    for iou, ref_index, candidate_index in sorted(pairs, reverse=True):
        if iou < minimum_iou:
            break
        if ref_index in used_reference or candidate_index in used_candidate:
            continue
        used_reference.add(ref_index)
        used_candidate.add(candidate_index)
        matches.append((reference[ref_index], candidate[candidate_index], iou))
    return matches, len(reference) - len(matches), len(candidate) - len(matches)


def compare_results(manifest: dict[str, Any], verified: list[dict[str, Any]]) -> dict[str, Any]:
    artifact_paths = {entry["name"]: Path(entry["path"]) for entry in verified}
    reference = read_results(artifact_paths["reference_results"])
    candidate = read_results(artifact_paths["candidate_results"])
    reference_ids, candidate_ids = set(reference), set(candidate)
    common_ids = sorted(reference_ids & candidate_ids)
    missing_ids = sorted(reference_ids - candidate_ids)
    extra_ids = sorted(candidate_ids - reference_ids)

    required_types = set(manifest["workload_types"])
    observed_reference_types = {record["task_type"] for record in reference.values()}
    observed_candidate_types = {record["task_type"] for record in candidate.values()}
    if not required_types <= observed_reference_types or not required_types <= observed_candidate_types:
        raise AcceptanceError("results do not contain every manifest workload_type")

    detector_ious: list[float] = []
    detector_confidence_errors: list[float] = []
    detector_class_matches = 0
    matched_detections = 0
    reference_detections = 0
    candidate_detections = 0
    unmatched_reference = 0
    unmatched_candidate = 0
    nms_different_frames = 0
    detection_frames = 0

    classification_frames = 0
    classification_top1_matches = 0
    classification_probability_errors: list[float] = []

    minimum_iou = manifest["gates"]["detector_match_iou_min"]
    for sample_id in common_ids:
        expected, actual = reference[sample_id], candidate[sample_id]
        if expected["task_type"] != actual["task_type"]:
            raise AcceptanceError(f"sample {sample_id}: task_type mismatch")
        if expected["task_type"] == "detection":
            detection_frames += 1
            expected_detections = expected["detections"]
            actual_detections = actual["detections"]
            reference_detections += len(expected_detections)
            candidate_detections += len(actual_detections)
            matches, missing, extra = match_detections(expected_detections, actual_detections, minimum_iou)
            unmatched_reference += missing
            unmatched_candidate += extra
            if missing or extra:
                nms_different_frames += 1
            expected_post = expected.get("nms", {}).get("post_count", len(expected_detections))
            actual_post = actual.get("nms", {}).get("post_count", len(actual_detections))
            if expected_post != actual_post and not (missing or extra):
                nms_different_frames += 1
            for expected_detection, actual_detection, iou in matches:
                matched_detections += 1
                detector_ious.append(iou)
                detector_confidence_errors.append(
                    abs(expected_detection["confidence"] - actual_detection["confidence"])
                )
                detector_class_matches += int(expected_detection["class_id"] == actual_detection["class_id"])
        else:
            classification_frames += 1
            expected_probabilities = expected["probabilities"]
            actual_probabilities = actual["probabilities"]
            if len(expected_probabilities) != len(actual_probabilities):
                raise AcceptanceError(f"sample {sample_id}: probability vector length mismatch")
            expected_top1 = expected.get("top1", max(range(len(expected_probabilities)), key=expected_probabilities.__getitem__))
            actual_top1 = actual.get("top1", max(range(len(actual_probabilities)), key=actual_probabilities.__getitem__))
            classification_top1_matches += int(expected_top1 == actual_top1)
            classification_probability_errors.extend(
                abs(left - right) for left, right in zip(expected_probabilities, actual_probabilities)
            )

    unmatched_total = unmatched_reference + unmatched_candidate
    detection_total = reference_detections + candidate_detections
    metrics = {
        "sample_coverage": {
            "reference_samples": len(reference_ids),
            "candidate_samples": len(candidate_ids),
            "common_samples": len(common_ids),
            "missing_sample_ids": missing_ids,
            "extra_sample_ids": extra_ids,
            "exact": reference_ids == candidate_ids,
        },
        "detection": {
            "frames": detection_frames,
            "reference_detections": reference_detections,
            "candidate_detections": candidate_detections,
            "matched_detections": matched_detections,
            "unmatched_reference": unmatched_reference,
            "unmatched_candidate": unmatched_candidate,
            "unmatched_rate": unmatched_total / detection_total if detection_total else None,
            "class_consistency": detector_class_matches / matched_detections if matched_detections else None,
            "box_iou_median": statistics.median(detector_ious) if detector_ious else None,
            "box_iou_p5": percentile(detector_ious, 0.05),
            "confidence_abs_error_p99": percentile(detector_confidence_errors, 0.99),
            "nms_different_frames": nms_different_frames,
            "nms_difference_rate": nms_different_frames / detection_frames if detection_frames else None,
        },
        "classification": {
            "frames": classification_frames,
            "top1_consistency": (
                classification_top1_matches / classification_frames if classification_frames else None
            ),
            "probability_abs_error_p99": percentile(classification_probability_errors, 0.99),
        },
    }
    report = {
        "schema_version": SCHEMA_VERSION,
        "dataset_id": manifest["dataset_id"],
        "reference_backend": manifest["reference_backend"],
        "candidate_backend": manifest["candidate_backend"],
        "workload_types": manifest["workload_types"],
        "verified_artifacts": verified,
        "postprocess": manifest.get("postprocess", {}),
        "metrics": metrics,
    }
    report["gates"] = evaluate_gates(report, manifest["gates"])
    report["passed"] = all(gate["passed"] for gate in report["gates"])
    report["business_accuracy_status"] = "passed" if report["passed"] else "failed"
    return report


def gate(name: str, actual: Any, operator: str, threshold: Any, applicable: bool = True) -> dict[str, Any]:
    if not applicable or actual is None:
        return {"name": name, "actual": actual, "operator": operator, "threshold": threshold,
                "applicable": applicable, "passed": not applicable}
    if operator == ">=":
        passed = actual >= threshold
    elif operator == "<=":
        passed = actual <= threshold
    elif operator == "==":
        passed = actual == threshold
    else:
        raise AssertionError(operator)
    return {"name": name, "actual": actual, "operator": operator, "threshold": threshold,
            "applicable": True, "passed": passed}


def evaluate_gates(report: dict[str, Any], thresholds: dict[str, float]) -> list[dict[str, Any]]:
    metrics = report["metrics"]
    detection = metrics["detection"]
    classification = metrics["classification"]
    detection_required = "detection" in report["workload_types"]
    classification_required = "classification" in report["workload_types"]
    return [
        gate("sample_ids_exact", metrics["sample_coverage"]["exact"], "==", True),
        gate("minimum_matched_detections", detection["matched_detections"], ">=",
             thresholds["minimum_matched_detections"], detection_required),
        gate("detector_class_consistency", detection["class_consistency"], ">=",
             thresholds["detector_class_consistency_min"], detection_required),
        gate("detector_box_iou_median", detection["box_iou_median"], ">=",
             thresholds["detector_box_iou_median_min"], detection_required),
        gate("detector_box_iou_p5", detection["box_iou_p5"], ">=",
             thresholds["detector_box_iou_p5_min"], detection_required),
        gate("detector_confidence_abs_error_p99", detection["confidence_abs_error_p99"], "<=",
             thresholds["detector_confidence_abs_error_p99_max"], detection_required),
        gate("detector_unmatched_rate", detection["unmatched_rate"], "<=",
             thresholds["detector_unmatched_rate_max"], detection_required),
        gate("detector_nms_difference_rate", detection["nms_difference_rate"], "<=",
             thresholds["detector_nms_difference_rate_max"], detection_required),
        gate("classification_top1_consistency", classification["top1_consistency"], ">=",
             thresholds["classification_top1_consistency_min"], classification_required),
        gate("classification_probability_abs_error_p99", classification["probability_abs_error_p99"], "<=",
             thresholds["classification_probability_abs_error_p99_max"], classification_required),
    ]


def write_report(output: Path, report: dict[str, Any]) -> None:
    output.mkdir(parents=True, exist_ok=False)
    json_path = output / "accuracy-report.json"
    markdown_path = output / "accuracy-report.md"
    json_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    rows = [
        "# Offline Business Accuracy Comparison Report",
        "",
        f"- Dataset: `{report['dataset_id']}`",
        f"- Reference: `{report['reference_backend']}`",
        f"- Candidate: `{report['candidate_backend']}`",
        f"- Result: `{'PASS' if report['passed'] else 'FAIL'}`",
        "",
        "| Gate | Actual | Requirement | Result |",
        "| --- | ---: | ---: | --- |",
    ]
    for item in report["gates"]:
        if not item["applicable"]:
            result = "N/A"
        else:
            result = "PASS" if item["passed"] else "FAIL"
        rows.append(f"| {item['name']} | {item['actual']} | {item['operator']} {item['threshold']} | {result} |")
    rows.extend(("", "This report compares exported per-frame outputs. Human review of false positives, "
                        "false negatives, lighting, occlusion and site-specific behavior remains required.", ""))
    markdown_path.write_text("\n".join(rows), encoding="utf-8")
    sums = [f"{sha256_file(path)}  {path.name}" for path in (json_path, markdown_path)]
    (output / "SHA256SUMS").write_text("\n".join(sums) + "\n", encoding="utf-8")


def init_workspace(output: Path) -> None:
    output.mkdir(parents=True, exist_ok=False)
    for directory in ("inputs", "results/reference", "results/candidate", "reports"):
        (output / directory).mkdir(parents=True, exist_ok=True)
    template = {
        "schema_version": SCHEMA_VERSION,
        "dataset_id": "REPLACE_WITH_VERSIONED_DATASET_ID",
        "reference_backend": "reference-backend",
        "candidate_backend": "candidate-backend",
        "workload_types": ["detection"],
        "artifacts": {
            "model": {"path": "inputs/model.onnx", "sha256": "REPLACE_WITH_SHA256"},
            "videos": [{"path": "inputs/acceptance.mp4", "sha256": "REPLACE_WITH_SHA256"}],
            "config": {"path": "inputs/config.json", "sha256": "REPLACE_WITH_SHA256"},
            "labels": {"path": "inputs/labels.txt", "sha256": "REPLACE_WITH_SHA256"},
            "reference_results": {"path": "results/reference/results.jsonl", "sha256": "REPLACE_WITH_SHA256"},
            "candidate_results": {"path": "results/candidate/results.jsonl", "sha256": "REPLACE_WITH_SHA256"},
        },
        "postprocess": {"confidence_threshold": None, "nms_iou_threshold": None},
        "gates": DEFAULT_GATES,
    }
    (output / "manifest.template.json").write_text(json.dumps(template, indent=2) + "\n", encoding="utf-8")
    sample = {"sample_id": "video-01:frame=000001", "task_type": "detection", "detections": [
        {"class_id": 0, "confidence": 0.95, "box_xyxy": [10.0, 20.0, 100.0, 200.0]}
    ], "nms": {"pre_count": 2, "post_count": 1}}
    (output / "results/example-result.jsonl").write_text(json.dumps(sample) + "\n", encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    init_parser = subparsers.add_parser("init", help="create a non-runnable acceptance workspace template")
    init_parser.add_argument("--output", type=Path, required=True)
    compare_parser = subparsers.add_parser("compare", help="verify hashes and compare exported outputs")
    compare_parser.add_argument("--manifest", type=Path, required=True)
    compare_parser.add_argument("--output", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        if args.command == "init":
            init_workspace(args.output.resolve())
            print(args.output.resolve())
            return 0
        manifest_path = args.manifest.resolve()
        manifest, verified = load_manifest(manifest_path)
        report = compare_results(manifest, verified)
        write_report(args.output.resolve(), report)
        print(json.dumps({"output": str(args.output.resolve()), "passed": report["passed"]}))
        return 0 if report["passed"] else 1
    except (AcceptanceError, OSError, ValueError) as error:
        print(str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
