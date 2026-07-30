import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
FIXTURES = Path(__file__).resolve().parent / "fixtures"
SCHEMAS = Path(__file__).resolve().parent / "schemas"
sys.path.insert(0, str(ROOT / "tools"))

import agent_workflow  # noqa: E402


class AgentWorkflowTest(unittest.TestCase):
    def test_contract_schema_and_runtime_validator_share_thin_required_shell(self):
        schema = json.loads((SCHEMAS / "task-contract-v1.schema.json").read_text(encoding="utf-8"))
        self.assertEqual(tuple(schema["required"]), agent_workflow.REQUIRED_CONTRACT_FIELDS)
        contract = json.loads((FIXTURES / "task-contract.valid.json").read_text(encoding="utf-8"))
        validated = agent_workflow.validate_contract(contract)
        self.assertTrue(validated["futureField"]["allowed"])

    def test_invalid_contract_is_rejected(self):
        contract = json.loads(
            (FIXTURES / "task-contract.invalid-missing-objective.json").read_text(encoding="utf-8")
        )
        with self.assertRaisesRegex(agent_workflow.WorkflowError, "userObjective"):
            agent_workflow.validate_contract(contract)

    def test_contract_path_must_stay_in_current_run_and_match_run_id(self):
        fixture = json.loads((FIXTURES / "task-contract.valid.json").read_text(encoding="utf-8"))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            run_dir = root / "output" / "agent-runs" / fixture["runId"]
            run_dir.mkdir(parents=True)
            contract_path = run_dir / "task-contract.json"
            contract_path.write_text(json.dumps(fixture), encoding="utf-8")
            resolved, resolved_run, _ = agent_workflow.resolve_contract_context(contract_path, root)
            self.assertEqual(resolved, contract_path.resolve())
            self.assertEqual(resolved_run, run_dir.resolve())

            outside = root / "outside.json"
            outside.write_text(json.dumps(fixture), encoding="utf-8")
            with self.assertRaisesRegex(agent_workflow.WorkflowError, "must stay under"):
                agent_workflow.resolve_contract_context(outside, root)

            fixture["runId"] = "different-run"
            contract_path.write_text(json.dumps(fixture), encoding="utf-8")
            with self.assertRaisesRegex(agent_workflow.WorkflowError, "must match"):
                agent_workflow.resolve_contract_context(contract_path, root)

    def test_run_input_rejects_parent_traversal_and_symlink_escape(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            run_dir = root / "output" / "agent-runs" / "run"
            run_dir.mkdir(parents=True)
            inside = run_dir / "model.onnx"
            inside.write_bytes(b"fixture")
            outside = root / "private.onnx"
            outside.write_bytes(b"fixture")
            self.assertEqual(agent_workflow.resolve_run_input(run_dir, "model.onnx"), inside.resolve())
            with self.assertRaisesRegex(agent_workflow.WorkflowError, "escapes"):
                agent_workflow.resolve_run_input(run_dir, "../../../private.onnx")
            link = run_dir / "link.onnx"
            try:
                link.symlink_to(outside)
            except OSError:
                self.skipTest("symlinks are not available")
            with self.assertRaisesRegex(agent_workflow.WorkflowError, "escapes"):
                agent_workflow.resolve_run_input(run_dir, "link.onnx")

    def test_environment_status_mapping(self):
        cases = json.loads((FIXTURES / "environment-status-cases.json").read_text(encoding="utf-8"))
        for case in cases:
            with self.subTest(case=case["name"]):
                self.assertEqual(agent_workflow.environment_verdict(case["checks"]), case["expected"])

    def test_python_version_requirements_support_exact_and_minimum_without_freezing_all_tasks(self):
        self.assertTrue(agent_workflow._version_satisfies("1.20.1", "1.20.1"))
        self.assertTrue(agent_workflow._version_satisfies("1.20.1", "==1.20.1"))
        self.assertTrue(agent_workflow._version_satisfies("1.26.0", ">=1.20.1"))
        self.assertFalse(agent_workflow._version_satisfies("1.19.0", ">=1.20.1"))
        self.assertTrue(agent_workflow._version_satisfies("9.9.9", None))

    def test_command_redaction(self):
        cases = json.loads((FIXTURES / "redaction-cases.json").read_text(encoding="utf-8"))
        for case in cases:
            with self.subTest(value=case["input"]):
                self.assertEqual(agent_workflow.redact_text(case["input"]), case["expected"])
        nested = {
            "authority": {
                "workspace": "current checkout",
                "credentialReference": "example-only",
                "note": "curl https://example-user:example-pass@example.invalid",
            }
        }
        self.assertEqual(agent_workflow.redact_data(nested)["authority"]["credentialReference"], "[REDACTED]")
        self.assertNotIn("example-pass", agent_workflow.redact_data(nested)["authority"]["note"])

    def test_baseline_is_inventory_not_task_readiness(self):
        process = subprocess.run(
            [str(ROOT / "scripts" / "agent" / "doctor.sh"), "--baseline", "--format", "json"],
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(process.returncode, 0, process.stderr)
        report = json.loads(process.stdout)
        self.assertEqual(report["mode"], "baseline")
        self.assertNotIn("environmentVerdict", report)
        self.assertFalse(report["authority"]["environmentChanges"])
        self.assertFalse(report["authority"]["externalSystems"])


if __name__ == "__main__":
    unittest.main()
