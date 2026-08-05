#!/usr/bin/env python3
"""Qualify fixed-frame YOLOv8 detection parity between legacy and fast paths."""

from __future__ import annotations

import argparse
import json
import math
from collections import Counter
from pathlib import Path

import numpy as np

from compare_yolov8_detections import decode, load_values, match


def parse_shape(raw: str) -> list[int]:
    try:
        shape = [int(value) for value in raw.lower().split("x")]
    except ValueError as error:
        raise argparse.ArgumentTypeError("shape must contain integer dimensions") from error
    if not shape or any(value <= 0 for value in shape):
        raise argparse.ArgumentTypeError("shape dimensions must be positive")
    return shape


def invalid_box_counts(detections: list[dict], input_size: int) -> tuple[int, int]:
    invalid = 0
    out_of_bounds = 0
    for detection in detections:
        box = detection["box"]
        values = [*box, detection["score"]]
        if not all(math.isfinite(value) for value in values) or box[2] <= box[0] or box[3] <= box[1]:
            invalid += 1
        if box[0] < 0 or box[1] < 0 or box[2] > input_size or box[3] > input_size:
            out_of_bounds += 1
    return invalid, out_of_bounds


def output_files(directory: Path) -> list[Path]:
    return sorted(directory.glob("*.f32.bin"))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--reference-dir", required=True, type=Path)
    parser.add_argument("--actual-dir", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    parser.add_argument("--shape", type=parse_shape, default=parse_shape("1x84x8400"))
    parser.add_argument("--input-size", type=int, default=640)
    parser.add_argument("--confidence", type=float, default=0.25)
    parser.add_argument("--nms", type=float, default=0.7)
    parser.add_argument("--match-iou", type=float, default=0.5)
    parser.add_argument("--top-k", type=int, default=1000)
    parser.add_argument("--class-id", type=int, action="append")
    parser.add_argument("--minimum-precision", type=float, default=0.99)
    parser.add_argument("--minimum-recall", type=float, default=0.99)
    parser.add_argument("--minimum-median-iou", type=float, default=0.99)
    parser.add_argument("--minimum-p5-iou", type=float, default=0.95)
    parser.add_argument("--maximum-confidence-difference", type=float, default=0.03)
    parser.add_argument("--maximum-class-count-delta-ratio", type=float, default=0.01)
    args = parser.parse_args()

    reference_paths = output_files(args.reference_dir)
    actual_paths = output_files(args.actual_dir)
    if not reference_paths:
        parser.error("reference directory contains no *.f32.bin files")
    if [path.name for path in reference_paths] != [path.name for path in actual_paths]:
        parser.error("reference and actual file sets differ")
    class_filter = set(args.class_id) if args.class_id else None

    ious: list[float] = []
    score_errors: list[float] = []
    reference_total = 0
    actual_total = 0
    matched_total = 0
    reference_classes: Counter[int] = Counter()
    actual_classes: Counter[int] = Counter()
    empty_regressions = 0
    nonfinite_frames = 0
    reference_invalid = 0
    actual_invalid = 0
    reference_out_of_bounds = 0
    actual_out_of_bounds = 0
    frames = []

    for reference_path, actual_path in zip(reference_paths, actual_paths):
        reference_values = load_values(reference_path, args.shape)
        actual_values = load_values(actual_path, args.shape)
        if not np.isfinite(reference_values).all() or not np.isfinite(actual_values).all():
            nonfinite_frames += 1
        reference = decode(reference_values, args.confidence, args.nms, args.top_k, class_filter)
        actual = decode(actual_values, args.confidence, args.nms, args.top_k, class_filter)
        matching = match(reference, actual, args.match_iou)
        frame_ious = [item["iou"] for item in matching["matches"]]
        frame_score_errors = [item["score_error"] for item in matching["matches"]]
        ious.extend(frame_ious)
        score_errors.extend(frame_score_errors)
        reference_total += len(reference)
        actual_total += len(actual)
        matched_total += len(frame_ious)
        reference_classes.update(item["class_id"] for item in reference)
        actual_classes.update(item["class_id"] for item in actual)
        empty_regressions += int(bool(reference) and not actual)
        ref_invalid, ref_oob = invalid_box_counts(reference, args.input_size)
        act_invalid, act_oob = invalid_box_counts(actual, args.input_size)
        reference_invalid += ref_invalid
        actual_invalid += act_invalid
        reference_out_of_bounds += ref_oob
        actual_out_of_bounds += act_oob
        frames.append(
            {
                "sample": reference_path.name.removesuffix(".f32.bin"),
                "reference_detections": len(reference),
                "actual_detections": len(actual),
                "matched_detections": len(frame_ious),
                "median_iou": float(np.median(frame_ious)) if frame_ious else None,
                "maximum_confidence_difference": max(frame_score_errors, default=0.0),
            }
        )

    precision = matched_total / actual_total if actual_total else float(reference_total == 0)
    recall = matched_total / reference_total if reference_total else float(actual_total == 0)
    all_class_ids = sorted(reference_classes.keys() | actual_classes.keys())
    class_count_deltas = {
        str(class_id): actual_classes[class_id] - reference_classes[class_id]
        for class_id in all_class_ids
    }
    maximum_class_delta = max((abs(value) for value in class_count_deltas.values()), default=0)
    allowed_class_delta = max(
        1, math.ceil(reference_total * args.maximum_class_count_delta_ratio)
    )
    metrics = {
        "frames": len(reference_paths),
        "reference_detections": reference_total,
        "actual_detections": actual_total,
        "matched_detections": matched_total,
        "precision": precision,
        "recall": recall,
        "median_iou": float(np.median(ious)) if ious else 0.0,
        "p5_iou": float(np.percentile(ious, 5)) if ious else 0.0,
        "maximum_confidence_difference": max(score_errors, default=0.0),
        "median_confidence_difference": float(np.median(score_errors)) if score_errors else 0.0,
        "reference_class_histogram": dict(sorted(reference_classes.items())),
        "actual_class_histogram": dict(sorted(actual_classes.items())),
        "class_count_deltas": class_count_deltas,
        "maximum_class_count_delta": maximum_class_delta,
        "allowed_class_count_delta": allowed_class_delta,
        "empty_regressions": empty_regressions,
        "nonfinite_frames": nonfinite_frames,
        "reference_invalid_boxes": reference_invalid,
        "actual_invalid_boxes": actual_invalid,
        "reference_out_of_bounds_boxes": reference_out_of_bounds,
        "actual_out_of_bounds_boxes": actual_out_of_bounds,
    }
    gates = {
        "precision": precision >= args.minimum_precision,
        "recall": recall >= args.minimum_recall,
        "median_iou": metrics["median_iou"] >= args.minimum_median_iou,
        "p5_iou": metrics["p5_iou"] >= args.minimum_p5_iou,
        "confidence_difference": metrics["maximum_confidence_difference"]
        <= args.maximum_confidence_difference,
        "no_systematic_class_change": maximum_class_delta <= allowed_class_delta,
        "no_new_empty_results": empty_regressions == 0,
        "no_nonfinite_results": nonfinite_frames == 0,
        "no_new_invalid_boxes": actual_invalid <= reference_invalid,
        "no_new_out_of_bounds_boxes": actual_out_of_bounds <= reference_out_of_bounds,
    }
    report = {
        "passed": all(gates.values()),
        "inputs": {
            "reference_dir": str(args.reference_dir.resolve()),
            "actual_dir": str(args.actual_dir.resolve()),
            "shape": args.shape,
            "class_filter": sorted(class_filter) if class_filter else None,
            "confidence": args.confidence,
            "nms": args.nms,
            "match_iou": args.match_iou,
        },
        "limits": {
            "minimum_precision": args.minimum_precision,
            "minimum_recall": args.minimum_recall,
            "minimum_median_iou": args.minimum_median_iou,
            "minimum_p5_iou": args.minimum_p5_iou,
            "maximum_confidence_difference": args.maximum_confidence_difference,
            "maximum_class_count_delta_ratio": args.maximum_class_count_delta_ratio,
        },
        "gates": gates,
        "metrics": metrics,
        "frame_results": frames,
    }
    serialized = json.dumps(report, indent=2, sort_keys=True) + "\n"
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(serialized, encoding="utf-8")
    print(serialized, end="")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
