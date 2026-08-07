#!/usr/bin/env python3
"""Create deterministic RKNN calibration and numerical-validation inputs."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import cv2
import numpy as np
import onnxruntime as ort


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def resize_image(image: np.ndarray, config: dict) -> tuple[np.ndarray, dict]:
    _, _, target_h, target_w = config["shape"]
    if config["resize"] == "stretch":
        resized = cv2.resize(image, (target_w, target_h), interpolation=cv2.INTER_LINEAR)
        return resized, {"scale": [target_w / image.shape[1], target_h / image.shape[0]], "pad": [0, 0]}
    if config["resize"] != "letterbox_center":
        raise ValueError(f"unsupported resize mode: {config['resize']}")
    scale = min(target_w / image.shape[1], target_h / image.shape[0])
    new_w = int(image.shape[1] * scale)
    new_h = int(image.shape[0] * scale)
    resized = cv2.resize(image, (new_w, new_h), interpolation=cv2.INTER_LINEAR)
    canvas = np.full((target_h, target_w, 3), config["padding_color"], dtype=np.uint8)
    pad_x = (target_w - new_w) // 2
    pad_y = (target_h - new_h) // 2
    canvas[pad_y : pad_y + new_h, pad_x : pad_x + new_w] = resized
    return canvas, {"scale": [scale, scale], "pad": [pad_x, pad_y]}


def preprocess(image: np.ndarray, config: dict) -> tuple[np.ndarray, dict]:
    resized, transform = resize_image(image, config)
    if config["color"] == "RGB":
        resized = cv2.cvtColor(resized, cv2.COLOR_BGR2RGB)
    elif config["color"] != "BGR":
        raise ValueError(f"unsupported color format: {config['color']}")
    values = resized.astype(np.float32)
    mean = np.asarray(config["mean"], dtype=np.float32).reshape(1, 1, 3)
    values = (values - mean) * np.float32(config["scale"])
    if config["layout"] == "NCHW":
        values = np.transpose(values, (2, 0, 1))[None]
    else:
        raise ValueError(f"unsupported layout: {config['layout']}")
    return np.ascontiguousarray(values), transform


def iou_one_to_many(box: np.ndarray, boxes: np.ndarray) -> np.ndarray:
    x1 = np.maximum(box[0], boxes[:, 0])
    y1 = np.maximum(box[1], boxes[:, 1])
    x2 = np.minimum(box[2], boxes[:, 2])
    y2 = np.minimum(box[3], boxes[:, 3])
    intersection = np.maximum(0.0, x2 - x1) * np.maximum(0.0, y2 - y1)
    box_area = max(0.0, box[2] - box[0]) * max(0.0, box[3] - box[1])
    areas = np.maximum(0.0, boxes[:, 2] - boxes[:, 0]) * np.maximum(0.0, boxes[:, 3] - boxes[:, 1])
    return intersection / np.maximum(box_area + areas - intersection, 1e-9)


def nms(boxes: np.ndarray, scores: np.ndarray, threshold: float) -> list[int]:
    order = np.argsort(scores)[::-1]
    keep: list[int] = []
    while order.size:
        current = int(order[0])
        keep.append(current)
        if order.size == 1:
            break
        remaining = order[1:]
        order = remaining[iou_one_to_many(boxes[current], boxes[remaining]) <= threshold]
    return keep


class PersonDetector:
    def __init__(self, model_path: Path, confidence: float = 0.25, nms_threshold: float = 0.45):
        self.session = ort.InferenceSession(str(model_path), providers=["CPUExecutionProvider"])
        self.input_name = self.session.get_inputs()[0].name
        self.confidence = confidence
        self.nms_threshold = nms_threshold
        self.config = {
            "shape": [1, 3, 640, 640],
            "resize": "letterbox_center",
            "padding_color": [114, 114, 114],
            "color": "RGB",
            "mean": [0.0, 0.0, 0.0],
            "scale": 1.0 / 255.0,
            "layout": "NCHW",
        }

    def detect(self, image: np.ndarray) -> list[dict]:
        tensor, transform = preprocess(image, self.config)
        output = self.session.run(None, {self.input_name: tensor})[0][0]
        scores = output[4]
        selected = np.flatnonzero(scores >= self.confidence)
        if selected.size == 0:
            return []
        center_boxes = output[:4, selected].T
        boxes = np.empty_like(center_boxes)
        boxes[:, 0] = center_boxes[:, 0] - center_boxes[:, 2] / 2
        boxes[:, 1] = center_boxes[:, 1] - center_boxes[:, 3] / 2
        boxes[:, 2] = center_boxes[:, 0] + center_boxes[:, 2] / 2
        boxes[:, 3] = center_boxes[:, 1] + center_boxes[:, 3] / 2
        chosen_scores = scores[selected]
        scale = transform["scale"][0]
        pad_x, pad_y = transform["pad"]
        boxes[:, [0, 2]] = (boxes[:, [0, 2]] - pad_x) / scale
        boxes[:, [1, 3]] = (boxes[:, [1, 3]] - pad_y) / scale
        boxes[:, [0, 2]] = np.clip(boxes[:, [0, 2]], 0, image.shape[1])
        boxes[:, [1, 3]] = np.clip(boxes[:, [1, 3]], 0, image.shape[0])
        results = []
        for index in nms(boxes, chosen_scores, self.nms_threshold):
            x1, y1, x2, y2 = boxes[index]
            if x2 - x1 >= 8 and y2 - y1 >= 8:
                results.append({"box": [float(x1), float(y1), float(x2), float(y2)], "score": float(chosen_scores[index])})
        return results


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--spec", required=True, type=Path)
    parser.add_argument("--video", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--samples", type=int, default=32)
    parser.add_argument("--frame-offset", type=int, default=0)
    parser.add_argument("--person-detector", type=Path)
    parser.add_argument("--max-crops-per-frame", type=int, default=3)
    args = parser.parse_args()
    if args.samples <= 0:
        parser.error("--samples must be positive")

    spec_path = args.spec.resolve()
    video_path = args.video.resolve()
    output_dir = args.output_dir.resolve()
    spec = json.loads(spec_path.read_text(encoding="utf-8"))
    if not video_path.is_file():
        parser.error(f"video does not exist: {video_path}")
    if spec["model_type"] == "classify" and args.person_detector is None:
        parser.error("classifier calibration requires --person-detector for representative person crops")
    detector = PersonDetector(args.person_detector.resolve()) if args.person_detector else None

    inputs_dir = output_dir / "inputs"
    sources_dir = output_dir / "sources"
    inputs_dir.mkdir(parents=True, exist_ok=True)
    sources_dir.mkdir(parents=True, exist_ok=True)
    capture = cv2.VideoCapture(str(video_path))
    if not capture.isOpened():
        raise RuntimeError(f"cannot decode video: {video_path}")
    frame_count = int(capture.get(cv2.CAP_PROP_FRAME_COUNT))
    # Oversample candidate frames because classifier preparation can yield
    # several or zero person crops per frame.
    candidate_count = min(frame_count, max(args.samples * 3, args.samples + args.frame_offset))
    frame_indices = np.linspace(args.frame_offset, frame_count - 1, candidate_count, dtype=int)

    records = []
    for frame_index in frame_indices:
        if len(records) >= args.samples:
            break
        capture.set(cv2.CAP_PROP_POS_FRAMES, int(frame_index))
        ok, frame = capture.read()
        if not ok:
            continue
        selections = [{"image": frame, "detection": None}]
        if detector:
            selections = []
            for detection in detector.detect(frame)[: args.max_crops_per_frame]:
                x1, y1, x2, y2 = [int(round(value)) for value in detection["box"]]
                crop = frame[max(0, y1) : min(frame.shape[0], y2), max(0, x1) : min(frame.shape[1], x2)]
                if crop.size:
                    selections.append({"image": crop, "detection": detection})
        for selection in selections:
            if len(records) >= args.samples:
                break
            sample_id = f"sample-{len(records):04d}"
            tensor, transform = preprocess(selection["image"], spec["input"])
            npy_path = inputs_dir / f"{sample_id}.npy"
            bin_path = inputs_dir / f"{sample_id}.f32.bin"
            image_path = sources_dir / f"{sample_id}.jpg"
            np.save(npy_path, tensor)
            tensor.tofile(bin_path)
            cv2.imwrite(str(image_path), selection["image"])
            records.append(
                {
                    "id": sample_id,
                    "frame_index": int(frame_index),
                    "detection": selection["detection"],
                    "transform": transform,
                    "tensor": {
                        "shape": list(tensor.shape),
                        "dtype": str(tensor.dtype),
                        "npy": str(npy_path),
                        "npy_sha256": sha256(npy_path),
                        "bin": str(bin_path),
                        "bin_sha256": sha256(bin_path),
                    },
                    "source_image": str(image_path),
                }
            )
    capture.release()
    if len(records) < args.samples:
        raise RuntimeError(f"only prepared {len(records)} of {args.samples} requested samples")

    dataset_path = output_dir / "dataset.txt"
    dataset_path.write_text("".join(record["tensor"]["npy"] + "\n" for record in records), encoding="utf-8")
    manifest = {
        "schema_version": 1,
        "spec": {"path": str(spec_path), "sha256": sha256(spec_path)},
        "video": {"path": str(video_path), "sha256": sha256(video_path), "frame_count": frame_count},
        "person_detector": (
            {"path": str(args.person_detector.resolve()), "sha256": sha256(args.person_detector.resolve())}
            if args.person_detector
            else None
        ),
        "labeled": False,
        "purpose": "representative calibration and numerical parity; not an accuracy benchmark",
        "samples": records,
        "dataset": {"path": str(dataset_path), "sha256": sha256(dataset_path)},
    }
    manifest_path = output_dir / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"prepared_samples={len(records)}")
    print(f"dataset={dataset_path}")
    print(f"manifest={manifest_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
