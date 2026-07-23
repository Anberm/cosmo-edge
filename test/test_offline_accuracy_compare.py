import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "offline_accuracy_compare.py"


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


class OfflineAccuracyCompareTest(unittest.TestCase):
    def make_workspace(self, root: Path, mismatch: bool = False) -> Path:
        inputs = root / "inputs"
        results = root / "results"
        inputs.mkdir()
        results.mkdir()
        for name, content in (("model.onnx", b"model"), ("video.mp4", b"video"),
                              ("config.json", b"{}"), ("labels.txt", b"helmet\n")):
            (inputs / name).write_bytes(content)

        reference = [
            {"sample_id": "frame-1", "task_type": "detection", "detections": [
                {"class_id": 0, "confidence": 0.91, "box_xyxy": [10, 10, 100, 100]}
            ], "nms": {"pre_count": 2, "post_count": 1}},
            {"sample_id": "frame-2", "task_type": "classification", "probabilities": [0.1, 0.9],
             "top1": 1},
        ]
        candidate = [
            {"sample_id": "frame-1", "task_type": "detection", "detections": [
                {"class_id": 1 if mismatch else 0, "confidence": 0.90,
                 "box_xyxy": [10.5, 10.5, 99.5, 99.5]}
            ], "nms": {"pre_count": 2, "post_count": 1}},
            {"sample_id": "frame-2", "task_type": "classification", "probabilities": [0.11, 0.89],
             "top1": 1},
        ]
        for name, rows in (("reference.jsonl", reference), ("candidate.jsonl", candidate)):
            (results / name).write_text("".join(json.dumps(row) + "\n" for row in rows), encoding="utf-8")

        artifacts = {
            "model": {"path": "inputs/model.onnx"},
            "videos": [{"path": "inputs/video.mp4"}],
            "config": {"path": "inputs/config.json"},
            "labels": {"path": "inputs/labels.txt"},
            "reference_results": {"path": "results/reference.jsonl"},
            "candidate_results": {"path": "results/candidate.jsonl"},
        }
        for name, spec in artifacts.items():
            if name == "videos":
                spec[0]["sha256"] = digest(root / spec[0]["path"])
            else:
                spec["sha256"] = digest(root / spec["path"])
        manifest = {
            "schema_version": 1,
            "dataset_id": "unit-test-v1",
            "reference_backend": "cpu-ort",
            "candidate_backend": "candidate-fp16",
            "workload_types": ["detection", "classification"],
            "artifacts": artifacts,
            "postprocess": {"confidence_threshold": 0.25, "nms_iou_threshold": 0.7},
        }
        manifest_path = root / "manifest.json"
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        return manifest_path

    def test_passing_detector_and_classifier_report(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = self.make_workspace(root)
            output = root / "report"
            process = subprocess.run(
                [sys.executable, str(TOOL), "compare", "--manifest", str(manifest), "--output", str(output)],
                text=True, capture_output=True, check=False,
            )
            self.assertEqual(process.returncode, 0, process.stderr)
            report = json.loads((output / "accuracy-report.json").read_text())
            self.assertTrue(report["passed"])
            self.assertEqual(report["metrics"]["detection"]["matched_detections"], 1)
            self.assertEqual(report["metrics"]["classification"]["top1_consistency"], 1.0)
            self.assertTrue((output / "SHA256SUMS").is_file())

    def test_class_mismatch_fails_gate(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = self.make_workspace(root, mismatch=True)
            output = root / "report"
            process = subprocess.run(
                [sys.executable, str(TOOL), "compare", "--manifest", str(manifest), "--output", str(output)],
                text=True, capture_output=True, check=False,
            )
            self.assertEqual(process.returncode, 1, process.stderr)
            report = json.loads((output / "accuracy-report.json").read_text())
            self.assertFalse(report["passed"])

    def test_hash_mismatch_is_invalid_not_a_failed_accuracy_gate(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest_path = self.make_workspace(root)
            manifest = json.loads(manifest_path.read_text())
            manifest["artifacts"]["model"]["sha256"] = "0" * 64
            manifest_path.write_text(json.dumps(manifest))
            process = subprocess.run(
                [sys.executable, str(TOOL), "compare", "--manifest", str(manifest_path),
                 "--output", str(root / "report")], text=True, capture_output=True, check=False,
            )
            self.assertEqual(process.returncode, 2)
            self.assertIn("SHA-256 mismatch", process.stderr)

    def test_init_is_explicitly_non_runnable_until_hashes_are_filled(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "workspace"
            process = subprocess.run(
                [sys.executable, str(TOOL), "init", "--output", str(output)],
                text=True, capture_output=True, check=False,
            )
            self.assertEqual(process.returncode, 0, process.stderr)
            self.assertTrue((output / "manifest.template.json").is_file())
            compare = subprocess.run(
                [sys.executable, str(TOOL), "compare", "--manifest", str(output / "manifest.template.json"),
                 "--output", str(output / "reports" / "run")], text=True, capture_output=True, check=False,
            )
            self.assertEqual(compare.returncode, 2)


if __name__ == "__main__":
    unittest.main()
