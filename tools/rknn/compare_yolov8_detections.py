#!/usr/bin/env python3
"""Compare decoded YOLOv8 detections rather than irrelevant low-score raw boxes."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np


def load_values(path: Path, shape: list[int]) -> np.ndarray:
    values = np.load(path) if path.suffix == ".npy" else np.fromfile(path, dtype=np.float32)
    if values.size != int(np.prod(shape)):
        raise ValueError(f"unexpected value count in {path}")
    return values.astype(np.float32, copy=False).reshape(shape)


def box_iou(box: np.ndarray, boxes: np.ndarray) -> np.ndarray:
    x1 = np.maximum(box[0], boxes[:, 0])
    y1 = np.maximum(box[1], boxes[:, 1])
    x2 = np.minimum(box[2], boxes[:, 2])
    y2 = np.minimum(box[3], boxes[:, 3])
    intersection = np.maximum(0.0, x2 - x1) * np.maximum(0.0, y2 - y1)
    area = max(0.0, box[2] - box[0]) * max(0.0, box[3] - box[1])
    other_area = np.maximum(0.0, boxes[:, 2] - boxes[:, 0]) * np.maximum(0.0, boxes[:, 3] - boxes[:, 1])
    return intersection / np.maximum(area + other_area - intersection, 1e-9)


def decode(
    output: np.ndarray,
    confidence: float,
    nms_threshold: float,
    top_k: int,
    class_filter: set[int] | None,
) -> list[dict]:
    values = output[0]
    class_ids = np.argmax(values[4:], axis=0)
    scores = values[4 + class_ids, np.arange(values.shape[1])]
    selected = np.flatnonzero(scores >= confidence)
    if class_filter is not None:
        class_mask = np.fromiter(
            (int(class_ids[index]) in class_filter for index in selected),
            dtype=np.bool_,
            count=selected.size,
        )
        selected = selected[class_mask]
    if selected.size == 0:
        return []
    centers = values[:4, selected].T
    boxes = np.empty_like(centers)
    boxes[:, 0] = centers[:, 0] - centers[:, 2] / 2
    boxes[:, 1] = centers[:, 1] - centers[:, 3] / 2
    boxes[:, 2] = centers[:, 0] + centers[:, 2] / 2
    boxes[:, 3] = centers[:, 1] + centers[:, 3] / 2
    selected_scores = scores[selected]
    selected_classes = class_ids[selected]
    keep = []
    for class_id in np.unique(selected_classes):
        candidates = np.flatnonzero(selected_classes == class_id)
        order = candidates[np.argsort(selected_scores[candidates])[::-1]]
        while order.size:
            current = int(order[0])
            keep.append(current)
            if order.size == 1:
                break
            remaining = order[1:]
            order = remaining[box_iou(boxes[current], boxes[remaining]) <= nms_threshold]
    keep = sorted(keep, key=lambda index: float(selected_scores[index]), reverse=True)[:top_k]
    return [
        {
            "class_id": int(selected_classes[index]),
            "score": float(selected_scores[index]),
            "box": [float(value) for value in boxes[index]],
        }
        for index in keep
    ]


def match(reference: list[dict], actual: list[dict], minimum_iou: float) -> dict:
    unmatched_actual = set(range(len(actual)))
    matches = []
    for reference_index, expected in enumerate(reference):
        candidates = [index for index in unmatched_actual if actual[index]["class_id"] == expected["class_id"]]
        if not candidates:
            continue
        expected_box = np.asarray(expected["box"], dtype=np.float32)
        candidate_boxes = np.asarray([actual[index]["box"] for index in candidates], dtype=np.float32)
        overlaps = box_iou(expected_box, candidate_boxes)
        best_offset = int(np.argmax(overlaps))
        if overlaps[best_offset] < minimum_iou:
            continue
        actual_index = candidates[best_offset]
        unmatched_actual.remove(actual_index)
        matches.append(
            {
                "reference_index": reference_index,
                "actual_index": actual_index,
                "class_id": expected["class_id"],
                "iou": float(overlaps[best_offset]),
                "score_error": abs(expected["score"] - actual[actual_index]["score"]),
            }
        )
    precision = len(matches) / len(actual) if actual else float(not reference)
    recall = len(matches) / len(reference) if reference else float(not actual)
    f1 = 2 * precision * recall / (precision + recall) if precision + recall else 0.0
    return {
        "reference_count": len(reference),
        "actual_count": len(actual),
        "matched_count": len(matches),
        "precision": precision,
        "recall": recall,
        "f1": f1,
        "mean_matched_iou": float(np.mean([item["iou"] for item in matches])) if matches else float(not reference and not actual),
        "maximum_matched_score_error": max((item["score_error"] for item in matches), default=0.0),
        "matches": matches,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--spec", required=True, type=Path)
    parser.add_argument("--precision", required=True, choices=("fp16", "int8"))
    parser.add_argument("--reference", required=True, type=Path)
    parser.add_argument("--actual", required=True, type=Path)
    parser.add_argument("--confidence", type=float, default=0.25)
    parser.add_argument("--nms", type=float, default=0.7)
    parser.add_argument("--match-iou", type=float, default=0.5)
    parser.add_argument("--top-k", type=int, default=1000)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()

    spec = json.loads(args.spec.read_text(encoding="utf-8"))
    shape = spec["outputs"][0]["shape"]
    class_ids = spec["validation"].get("class_ids")
    class_filter = set(int(value) for value in class_ids) if class_ids is not None else None
    expected = decode(load_values(args.reference, shape), args.confidence, args.nms, args.top_k, class_filter)
    observed = decode(load_values(args.actual, shape), args.confidence, args.nms, args.top_k, class_filter)
    metrics = match(expected, observed, args.match_iou)
    limits = spec["validation"][args.precision]
    passed = (
        metrics["f1"] >= limits["minimum_detection_f1"]
        and metrics["mean_matched_iou"] >= limits["minimum_matched_iou"]
        and metrics["maximum_matched_score_error"] <= limits["maximum_matched_score_error"]
    )
    result = {
        "passed": passed,
        "precision": args.precision,
        "thresholds": {"confidence": args.confidence, "nms": args.nms, "match_iou": args.match_iou},
        "class_ids": sorted(class_filter) if class_filter is not None else None,
        "limits": {
            key: limits[key]
            for key in ("minimum_detection_f1", "minimum_matched_iou", "maximum_matched_score_error")
        },
        "metrics": metrics,
        "reference_detections": expected,
        "actual_detections": observed,
    }
    serialized = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(serialized, encoding="utf-8")
    print(serialized, end="")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
