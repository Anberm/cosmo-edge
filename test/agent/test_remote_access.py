import inspect
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import agent_workflow as core  # noqa: E402
import remote_access  # noqa: E402


def make_remote_run(root: Path, *, authorized: bool = True) -> tuple[Path, Path, dict]:
    run_id = "remote-connect-test" if authorized else "remote-connect-no-grant"
    contract = {
        "schemaVersion": "1.0",
        "runId": run_id,
        "task": "model-conversion",
        "userObjective": "Inspect the supplied isolated development environment.",
        "expectedDeliverables": ["environment findings"],
        "allowedChanges": [f"output/agent-runs/{run_id}/"],
        "requiredCapabilities": [],
        "acceptance": {},
        "authority": {
            "workspace": "isolated test checkout",
            "grants": ["remote-execution"] if authorized else [],
        },
        "parameters": {
            "targetChip": "bm1688",
            "developmentEnvironment": {
                "os": "linux",
                "architecture": "x86_64",
                "reference": "isolated development environment",
            },
        },
    }
    run_dir = root / "output" / "agent-runs" / run_id
    run_dir.mkdir(parents=True)
    contract_path = run_dir / "task-contract.json"
    core.atomic_write_json(contract_path, contract)
    assessment = {
        "schemaVersion": "1.0",
        "mode": "assessment",
        "task": contract["task"],
        "runId": run_id,
        "contractSha256": core.sha256_file(contract_path),
        "needsInput": [
            {
                "id": "source-model",
                "category": "business-input",
                "question": "Provide the model material.",
                "reason": "The conversion candidate is not present yet.",
                "requiredBefore": "execution",
            }
        ],
        "routeVerdict": "NEEDS_INPUT",
    }
    core.atomic_write_json(run_dir / "route-assessment.json", assessment)
    return run_dir, contract_path, contract


class RemoteAccessTest(unittest.TestCase):
    def test_explicit_remote_grant_allows_interactive_connection_before_full_route_ready(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            run_dir, contract_path, _ = make_remote_run(root)
            commands = []

            def runner(command, *, check):
                commands.append(command)
                self.assertFalse(check)
                return subprocess.CompletedProcess(command, 0)

            original_which = remote_access.shutil.which
            remote_access.shutil.which = lambda name: "/usr/bin/ssh" if name == "ssh" else None
            try:
                result = remote_access.connect_remote(
                    contract_path,
                    host="dev-host.internal",
                    user="example-user",
                    project_root=root,
                    runner=runner,
                )
            finally:
                remote_access.shutil.which = original_which

            self.assertEqual(result, 0)
            self.assertEqual(commands[0][-2:], ["example-user", "dev-host.internal"])
            self.assertNotIn("password", inspect.signature(remote_access.connect_remote).parameters)
            events = json.loads(
                (run_dir / "remote-access-events.json").read_text(encoding="utf-8")
            )
            self.assertEqual(events[-1]["status"], "PASS")
            self.assertEqual(events[-1]["routeVerdictAtConnection"], "NEEDS_INPUT")
            self.assertFalse(events[-1]["credentialMaterialStored"])
            serialized = json.dumps(events)
            self.assertNotIn("dev-host.internal", serialized)
            self.assertNotIn("example-user", serialized)

    def test_remote_connection_still_requires_explicit_user_intent(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            _, contract_path, _ = make_remote_run(root, authorized=False)
            with self.assertRaisesRegex(core.WorkflowError, "not authorized"):
                remote_access.connect_remote(
                    contract_path,
                    host="dev-host.internal",
                    user="example-user",
                    project_root=root,
                )


if __name__ == "__main__":
    unittest.main()
