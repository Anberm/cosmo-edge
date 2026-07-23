#!/usr/bin/env python3
"""Validate GroundingDINO output semantics against a reference result."""

from __future__ import annotations

import argparse
import json
import math
import string
import sys
from pathlib import Path

import numpy as np


SPECIAL_TOKENS = {"[PAD]", "[CLS]", "[SEP]", "[MASK]"}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--case", required=True)
    parser.add_argument("--vocab", required=True, type=Path)
    parser.add_argument("--input-ids", required=True, type=Path)
    parser.add_argument("--reference-dir", required=True, type=Path)
    parser.add_argument("--candidate-dir", required=True, type=Path)
    parser.add_argument("--box-threshold", type=float, default=0.3)
    parser.add_argument("--text-threshold", type=float, default=0.25)
    parser.add_argument("--min-iou", type=float, default=0.95)
    parser.add_argument("--max-score-delta", type=float, default=0.05)
    return parser.parse_args()


def require_unit_interval(name: str, value: float) -> None:
    if not math.isfinite(value) or value < 0.0 or value > 1.0:
        raise ValueError(f"{name} must be finite and in [0, 1]")


def load_float_output(directory: Path, name: str) -> np.ndarray:
    preferred = directory / f"{name}.f32.bin"
    fallback = directory / f"{name}.bin"
    path = preferred if preferred.is_file() else fallback
    if not path.is_file():
        raise FileNotFoundError(f"missing {name} output in {directory}")
    values = np.fromfile(path, dtype=np.float32)
    if values.size == 0:
        raise ValueError(f"empty output: {path}")
    return values


def stable_sigmoid(values: np.ndarray) -> np.ndarray:
    result = np.empty_like(values)
    positive = values >= 0
    result[positive] = 1.0 / (1.0 + np.exp(-values[positive]))
    exponential = np.exp(values[~positive])
    result[~positive] = exponential / (1.0 + exponential)
    return result


def decode_wordpieces(vocab: list[str], ids: np.ndarray) -> str:
    phrase = ""
    for token_id in ids:
        index = int(token_id)
        if index < 0 or index >= len(vocab):
            continue
        token = vocab[index]
        if token in SPECIAL_TOKENS:
            continue
        if token.startswith("##"):
            phrase += token[2:]
            continue
        first = token[0] if token else ""
        needs_space = bool(first) and (ord(first) >= 128 or first not in string.punctuation)
        if phrase and needs_space:
            phrase += " "
        phrase += token
    return phrase


def xyxy(box: np.ndarray) -> list[float]:
    cx, cy, width, height = np.clip(box, 0.0, 1.0)
    return [
        float(np.clip(cx - width / 2.0, 0.0, 1.0)),
        float(np.clip(cy - height / 2.0, 0.0, 1.0)),
        float(np.clip(cx + width / 2.0, 0.0, 1.0)),
        float(np.clip(cy + height / 2.0, 0.0, 1.0)),
    ]


def detections(
    logits: np.ndarray,
    boxes: np.ndarray,
    input_ids: np.ndarray,
    vocab: list[str],
    box_threshold: float,
    text_threshold: float,
) -> list[dict[str, object]]:
    probabilities = stable_sigmoid(logits)
    result: list[dict[str, object]] = []
    for query in range(probabilities.shape[0]):
        token_scores = probabilities[query]
        score = float(np.max(token_scores))
        if not math.isfinite(score) or score <= box_threshold:
            continue
        selected = input_ids[1:][token_scores[1:] > text_threshold]
        phrase = decode_wordpieces(vocab, selected)
        if not phrase:
            continue
        result.append(
            {"query": query, "score": score, "phrase": phrase, "box": xyxy(boxes[query])}
        )
    return result


def iou(left: list[float], right: list[float]) -> float:
    x1 = max(left[0], right[0])
    y1 = max(left[1], right[1])
    x2 = min(left[2], right[2])
    y2 = min(left[3], right[3])
    intersection = max(0.0, x2 - x1) * max(0.0, y2 - y1)
    left_area = max(0.0, left[2] - left[0]) * max(0.0, left[3] - left[1])
    right_area = max(0.0, right[2] - right[0]) * max(0.0, right[3] - right[1])
    union = left_area + right_area - intersection
    return intersection / union if union > 0.0 else 0.0


def greedy_matches(reference: list[dict], candidate: list[dict]) -> list[dict[str, object]]:
    pairs = sorted(
        (
            (iou(ref["box"], cand["box"]), ref_index, candidate_index)
            for ref_index, ref in enumerate(reference)
            for candidate_index, cand in enumerate(candidate)
        ),
        reverse=True,
    )
    used_reference: set[int] = set()
    used_candidate: set[int] = set()
    matches: list[dict[str, object]] = []
    for overlap, ref_index, candidate_index in pairs:
        if ref_index in used_reference or candidate_index in used_candidate:
            continue
        used_reference.add(ref_index)
        used_candidate.add(candidate_index)
        ref = reference[ref_index]
        cand = candidate[candidate_index]
        matches.append(
            {
                "reference_query": ref["query"],
                "candidate_query": cand["query"],
                "iou": overlap,
                "reference_score": ref["score"],
                "candidate_score": cand["score"],
                "score_abs": abs(ref["score"] - cand["score"]),
                "reference_phrase": ref["phrase"],
                "candidate_phrase": cand["phrase"],
                "phrase_exact": ref["phrase"] == cand["phrase"],
            }
        )
    return matches


def main() -> int:
    args = parse_args()
    require_unit_interval("box threshold", args.box_threshold)
    require_unit_interval("text threshold", args.text_threshold)
    require_unit_interval("minimum IoU", args.min_iou)
    require_unit_interval("maximum score delta", args.max_score_delta)

    vocab = args.vocab.read_text(encoding="utf-8").splitlines()
    input_ids = np.fromfile(args.input_ids, dtype=np.int32)
    if input_ids.size < 3:
        raise ValueError("GroundingDINO input_ids must contain at least three tokens")

    ref_logits = load_float_output(args.reference_dir, "logits")
    ref_boxes = load_float_output(args.reference_dir, "boxes")
    candidate_logits = load_float_output(args.candidate_dir, "logits")
    candidate_boxes = load_float_output(args.candidate_dir, "boxes")
    if ref_boxes.size % 4 != 0 or candidate_boxes.size != ref_boxes.size:
        raise ValueError("reference and candidate box shapes do not match")
    queries = ref_boxes.size // 4
    expected_logits = queries * input_ids.size
    if ref_logits.size != expected_logits or candidate_logits.size != expected_logits:
        raise ValueError("reference and candidate logits do not match input token dimensions")

    ref_logits = ref_logits.reshape(queries, input_ids.size)
    candidate_logits = candidate_logits.reshape(queries, input_ids.size)
    ref_boxes = ref_boxes.reshape(queries, 4)
    candidate_boxes = candidate_boxes.reshape(queries, 4)
    reference = detections(
        ref_logits, ref_boxes, input_ids, vocab, args.box_threshold, args.text_threshold
    )
    candidate = detections(
        candidate_logits,
        candidate_boxes,
        input_ids,
        vocab,
        args.box_threshold,
        args.text_threshold,
    )
    matches = greedy_matches(reference, candidate)
    finite_logits = np.isfinite(candidate_logits) | np.isneginf(candidate_logits)
    checks = {
        "candidate_has_no_nan_or_positive_inf": bool(np.all(finite_logits)),
        "candidate_boxes_finite": bool(np.all(np.isfinite(candidate_boxes))),
        "detection_count_exact": len(reference) == len(candidate),
        "all_detections_matched": len(matches) == len(reference) == len(candidate),
        "iou_minimum": all(match["iou"] >= args.min_iou for match in matches),
        "score_delta_maximum": all(
            match["score_abs"] <= args.max_score_delta for match in matches
        ),
        "phrases_exact": all(match["phrase_exact"] for match in matches),
    }
    report = {
        "case": args.case,
        "thresholds": {
            "box": args.box_threshold,
            "text": args.text_threshold,
            "min_iou": args.min_iou,
            "max_score_delta": args.max_score_delta,
        },
        "reference_count": len(reference),
        "candidate_count": len(candidate),
        "matches": matches,
        "checks": checks,
        "pass": all(checks.values()),
    }
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if report["pass"] else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"GroundingDINO validation failed: {error}", file=sys.stderr)
        sys.exit(2)
