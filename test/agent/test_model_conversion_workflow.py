import json
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import agent_workflow as core  # noqa: E402
import check_onnx_model  # noqa: E402
import model_conversion_workflow as conversion  # noqa: E402


def make_contract(run_id: str = "conversion-test") -> dict:
    return {
        "schemaVersion": "1.0",
        "runId": run_id,
        "task": "model-conversion",
        "userObjective": "Convert the supplied ONNX model for a BM1688 development task.",
        "expectedDeliverables": ["F16 bmodel", "evidence"],
        "allowedChanges": ["current run directory"],
        "requiredCapabilities": ["python3"],
        "acceptance": {
            "requireTensorCompare": True,
            "promoteExample": False,
        },
        "authority": {
            "workspace": "isolated test checkout",
            "environmentChanges": False,
            "externalSystems": False,
        },
        "parameters": {
            "sourceModel": "inputs/candidate.onnx",
            "modelName": "candidate_320",
            "modelFamily": "fixture detector",
            "sourceUrl": "https://example.invalid/candidate.onnx",
            "recordedBy": "fixture-maintainer",
            "targetBackend": "Sophon BMRT",
            "targetChip": "bm1688",
            "toolchainChip": "bm1688",
            "quantization": "F16",
            "inputLayout": "NCHW",
            "inputShapes": [[1, 3, 320, 320]],
            "expectedOutputShapes": [[1, 6, 2100]],
            "pixelFormat": "rgb",
            "toolchain": {
                "kind": "python-package",
                "pythonExecutable": sys.executable,
                "package": "tpu_mlir",
                "version": "1.28.1",
            },
            "preflight": {
                "pythonExecutable": sys.executable,
                "pythonPackages": {
                    "numpy": None,
                    "onnx": None,
                    "onnxruntime": None,
                },
            },
            "tensorTolerance": "0.99,0.90",
            "outputKind": "bmodel",
        },
    }


def make_toolchain_identity() -> dict:
    return {
        "kind": "python-package",
        "id": "sha256:" + "a" * 64,
        "pythonExecutable": "/isolated/venv/bin/python",
        "pythonVersion": "3.10.12",
        "package": {
            "name": "tpu_mlir",
            "version": "1.28.1",
            "recordSha256": "b" * 64,
        },
        "tools": {
            "modelTransform": {
                "path": "/isolated/venv/bin/model_transform.py",
                "sha256": "c" * 64,
            },
            "modelDeploy": {
                "path": "/isolated/venv/bin/model_deploy.py",
                "sha256": "d" * 64,
            },
            "modelTool": {
                "path": "/isolated/venv/bin/model_tool",
                "sha256": "e" * 64,
            },
        },
    }


def prepare_run(root: Path, contract: dict) -> tuple[Path, Path]:
    run_dir = root / "output" / "agent-runs" / contract["runId"]
    (run_dir / "inputs").mkdir(parents=True)
    (run_dir / "inputs" / "candidate.onnx").write_bytes(b"synthetic-onnx-fixture")
    contract_path = run_dir / "task-contract.json"
    contract_path.write_text(json.dumps(contract), encoding="utf-8")
    return run_dir, contract_path


class ModelConversionWorkflowTest(unittest.TestCase):
    def test_example_catalog_starts_truthfully_empty_and_json_assets_parse(self):
        example_dir = ROOT / "test" / "agent" / "examples" / "model-conversion"
        index = json.loads((example_dir / "index.json").read_text(encoding="utf-8"))
        schema = json.loads((example_dir / "schema.json").read_text(encoding="utf-8"))
        self.assertEqual(index["examples"], [])
        self.assertIn("recordings", schema["required"])

    def test_candidate_shapes_and_chip_come_from_contract(self):
        with tempfile.TemporaryDirectory() as directory:
            run_dir, _ = prepare_run(Path(directory), make_contract())
            parameters = conversion.conversion_parameters(make_contract(), run_dir)
            transform = conversion.build_transform_arguments(
                parameters,
                source_model="/workspace/run/inputs/candidate.onnx",
                mlir="/workspace/run/work/candidate.mlir",
            )
            deploy = conversion.build_deploy_arguments(
                parameters,
                mlir="/workspace/run/work/candidate.mlir",
                model="/workspace/run/artifacts/candidate.bmodel",
            )
            self.assertIn("[[1,3,320,320]]", transform)
            self.assertNotIn("640", " ".join(transform))
            self.assertEqual(deploy[deploy.index("--chip") + 1], "bm1688")
            self.assertEqual(deploy[deploy.index("--quantize") + 1], "F16")

    def test_python_toolchain_uses_selected_interpreter_not_entry_shebang(self):
        identity = make_toolchain_identity()
        command = conversion._tool_command(
            identity,
            "modelTransform",
            ["--help"],
            run_dir=ROOT / "output" / "agent-runs" / "fixture",
        )
        self.assertEqual(command[0], identity["pythonExecutable"])
        self.assertEqual(command[1], identity["tools"]["modelTransform"]["path"])
        self.assertEqual(command[2:], ["--help"])

    def test_contract_source_cannot_escape_the_run(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            contract = make_contract()
            run_dir, _ = prepare_run(root, contract)
            outside = root / "outside.onnx"
            outside.write_bytes(b"synthetic")
            contract["parameters"]["sourceModel"] = "../../../outside.onnx"
            with self.assertRaisesRegex(core.WorkflowError, "escapes"):
                conversion.conversion_parameters(contract, run_dir)

    def test_dynamic_shape_resolution_is_bounded_and_explicit(self):
        self.assertEqual(check_onnx_model.runtime_shape([1, 3, "height", None], None), [1, 3, 1, 1])
        self.assertEqual(
            check_onnx_model.runtime_shape([1, 3, "height", "width"], [1, 3, 320, 320]),
            [1, 3, 320, 320],
        )
        with self.assertRaisesRegex(check_onnx_model.OnnxCheckError, "rank"):
            check_onnx_model.runtime_shape([1, 3, 320, 320], [1, 3, 320])

    def test_environment_report_is_bound_to_contract_hash(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            contract = make_contract()
            run_dir, contract_path = prepare_run(root, contract)
            report = {
                "runId": contract["runId"],
                "task": contract["task"],
                "contractSha256": core.sha256_file(contract_path),
                "environmentVerdict": "READY",
                "repository": core._git_snapshot(),
                "toolchain": make_toolchain_identity(),
            }
            (run_dir / "environment-report.json").write_text(json.dumps(report), encoding="utf-8")
            self.assertEqual(
                conversion._read_environment_report(contract_path, run_dir, contract)["environmentVerdict"],
                "READY",
            )
            contract["userObjective"] = "changed after admission"
            contract_path.write_text(json.dumps(contract), encoding="utf-8")
            with self.assertRaisesRegex(core.WorkflowError, "changed after"):
                conversion._read_environment_report(contract_path, run_dir, contract)

    def _make_verifiable_run(self, root: Path, tensor_status: str) -> tuple[Path, Path, dict]:
        contract = make_contract()
        run_dir, contract_path = prepare_run(root, contract)
        source_path = run_dir / "inputs" / "candidate.onnx"
        (run_dir / "verification").mkdir()
        preflight_path = run_dir / "verification" / "onnx-check.json"
        preflight = {
            "schemaVersion": "1.0",
            "status": "PASS",
            "model": {"sha256": core.sha256_file(source_path)},
        }
        preflight_path.write_text(json.dumps(preflight), encoding="utf-8")
        (run_dir / "artifacts").mkdir()
        artifact_path = run_dir / "artifacts" / "candidate_320_bm1688_f16.bmodel"
        artifact_path.write_bytes(b"synthetic-bmodel-fixture")
        environment = {
            "schemaVersion": "1.0",
            "runId": contract["runId"],
            "task": contract["task"],
            "commit": "1" * 40,
            "tree": "2" * 40,
            "contractSha256": core.sha256_file(contract_path),
            "environmentVerdict": "READY",
            "repository": core._git_snapshot(),
            "toolchain": make_toolchain_identity(),
        }
        (run_dir / "environment-report.json").write_text(json.dumps(environment), encoding="utf-8")
        selection = {
            "selectedAssets": ["docs/tutorials/05-model-porting/model-porting.md"],
            "differences": ["fixture uses 320x320 input"],
        }
        (run_dir / "asset-selection.json").write_text(json.dumps(selection), encoding="utf-8")
        manifest = {
            "schemaVersion": "1.0",
            "status": "COMPLETE",
            "runId": contract["runId"],
            "contractSha256": core.sha256_file(contract_path),
            "source": {
                "path": "inputs/candidate.onnx",
                "sha256": core.sha256_file(source_path),
            },
            "toolchain": environment["toolchain"],
            "stages": {
                "onnxPreflight": {
                    "status": "PASS",
                    "report": "verification/onnx-check.json",
                    "reportSha256": core.sha256_file(preflight_path),
                },
                "modelInfo": {
                    "status": "PASS",
                    "contractMatches": True,
                },
                "tensorCompare": {
                    "status": tensor_status,
                    "detail": "synthetic tensor result",
                    "tolerance": "0.99,0.90",
                },
            },
            "artifacts": [
                {
                    "path": "artifacts/candidate_320_bm1688_f16.bmodel",
                    "sha256": core.sha256_file(artifact_path),
                    "sizeBytes": artifact_path.stat().st_size,
                    "role": "bm1688-bmodel",
                }
            ],
            "commands": [
                "docker run --password [REDACTED] synthetic",
            ],
        }
        (run_dir / "execution-manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
        return run_dir, contract_path, contract

    def test_development_and_promotion_verdicts_are_independent(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            run_dir, contract_path, contract = self._make_verifiable_run(root, "UNVERIFIED")
            evidence = conversion.verify_conversion(contract_path, run_dir, contract)
            evidence_schema = json.loads(
                (ROOT / "test" / "agent" / "schemas" / "evidence-v1.schema.json").read_text(
                    encoding="utf-8"
                )
            )
            self.assertFalse(set(evidence_schema["required"]) - set(evidence))
            self.assertEqual(evidence["developmentVerdict"], "PARTIAL")
            self.assertEqual(evidence["promotionVerdict"], "NOT_REQUESTED")
            self.assertEqual(evidence["deviceVerdict"], "NOT_RUN")
            self.assertTrue((run_dir / "evidence.md").is_file())

            manifest_path = run_dir / "execution-manifest.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["stages"]["tensorCompare"]["status"] = "PASS"
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            complete = conversion.verify_conversion(contract_path, run_dir, contract)
            self.assertEqual(complete["developmentVerdict"], "COMPLETE")
            self.assertEqual(complete["promotionVerdict"], "NOT_REQUESTED")

            (run_dir / "inputs" / "candidate.onnx").write_bytes(b"changed-after-conversion")
            tampered = conversion.verify_conversion(contract_path, run_dir, contract)
            self.assertEqual(tampered["developmentVerdict"], "FAILED")
            self.assertEqual(
                next(stage for stage in tampered["stages"] if stage["id"] == "S1")["status"],
                "FAIL",
            )

    def test_example_needs_two_distinct_same_candidate_recordings(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            example_dir = root / "test" / "agent" / "examples" / "model-conversion"
            example_dir.mkdir(parents=True)
            run_dir, _, contract = self._make_verifiable_run(root, "PASS")
            parameters = conversion.conversion_parameters(contract, run_dir)
            manifest_path = run_dir / "execution-manifest.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest.update(
                {
                    "completedAt": "2026-01-01T00:00:00Z",
                    "commit": "1" * 40,
                    "tree": "2" * 40,
                    "repository": {
                        "trackedChanges": 0,
                        "untrackedFiles": 0,
                    },
                    "source": {
                        "path": "inputs/candidate.onnx",
                        "sha256": core.sha256_file(run_dir / "inputs" / "candidate.onnx"),
                    },
                    "target": {},
                }
            )
            manifest["toolchain"] = make_toolchain_identity()
            manifest_path.write_text(json.dumps({"recording": 1}), encoding="utf-8")
            example_path = example_dir / "candidate-bm1688-f16.json"
            first = conversion.record_example(
                str(example_path),
                manifest,
                parameters,
                run_dir=run_dir,
                project_root=root,
            )
            self.assertEqual(first["status"], "candidate")
            self.assertEqual(len(first["recordings"]), 1)

            manifest["completedAt"] = "2026-01-01T00:10:00Z"
            manifest_path.write_text(json.dumps({"recording": 2}), encoding="utf-8")
            second = conversion.record_example(
                str(example_path),
                manifest,
                parameters,
                run_dir=run_dir,
                project_root=root,
            )
            self.assertEqual(second["status"], "verified")
            self.assertEqual(len(second["recordings"]), 2)

            outside = root / "candidate-outside.json"
            with self.assertRaisesRegex(core.WorkflowError, "must stay under"):
                conversion.record_example(
                    str(outside),
                    manifest,
                    parameters,
                    run_dir=run_dir,
                    project_root=root,
                )


if __name__ == "__main__":
    unittest.main()
