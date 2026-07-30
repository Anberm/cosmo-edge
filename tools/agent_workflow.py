#!/usr/bin/env python3
"""Shared, dependency-free helpers for CosmoEdge agent-assisted workflows."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any, Iterable


PROJECT_ROOT = Path(__file__).resolve().parents[1]
RUN_ID_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$")
REQUIRED_CONTRACT_FIELDS = (
    "schemaVersion",
    "runId",
    "task",
    "userObjective",
    "expectedDeliverables",
    "allowedChanges",
    "requiredCapabilities",
    "acceptance",
    "authority",
)
ENVIRONMENT_VERDICTS = {"READY", "REPAIRABLE", "NEEDS_ENVIRONMENT", "UNSUPPORTED"}
CHECK_STATUSES = {"PASS", "FAIL", "BLOCKED", "SKIP", "UNVERIFIED"}
SENSITIVE_NAME = r"(?:password|passwd|pwd|token|api[_-]?key|authorization|credential|secret)"
IMAGE_REFERENCE_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._/@:-]+$")
TOOLCHAIN_KINDS = {"python-package", "container-image"}
TOOLCHAIN_PACKAGE_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]*$")
TOOLCHAIN_PROBE_SCRIPT = r"""
import hashlib
import importlib
import importlib.metadata
import json
import pathlib
import shutil
import sys

package = sys.argv[1]
distribution = importlib.metadata.distribution(package)
importlib.import_module(package.replace("-", "_"))
bin_dir = pathlib.Path(sys.executable).absolute().parent

def tool(names):
    for name in names:
        candidate = bin_dir / name
        if candidate.is_file():
            path = candidate.resolve()
            return {
                "path": str(path),
                "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            }
        resolved = shutil.which(name, path=str(bin_dir))
        if resolved:
            path = pathlib.Path(resolved).resolve()
            return {
                "path": str(path),
                "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            }
    raise RuntimeError("missing tool: " + " or ".join(names))

record = None
for entry in distribution.files or []:
    if str(entry).endswith(".dist-info/RECORD"):
        record = pathlib.Path(distribution.locate_file(entry)).resolve()
        break
if record is None or not record.is_file():
    raise RuntimeError("installed distribution has no RECORD identity")

print(json.dumps({
    "pythonExecutable": str(pathlib.Path(sys.executable).absolute()),
    "pythonVersion": sys.version.split()[0],
    "requiresPython": distribution.metadata.get("Requires-Python"),
    "package": {
        "name": distribution.metadata.get("Name") or package,
        "version": distribution.version,
        "recordSha256": hashlib.sha256(record.read_bytes()).hexdigest(),
    },
    "tools": {
        "modelTransform": tool(["model_transform.py", "model_transform"]),
        "modelDeploy": tool(["model_deploy.py", "model_deploy"]),
        "modelTool": tool(["model_tool"]),
    },
}, sort_keys=True))
"""


class WorkflowError(ValueError):
    """Raised for invalid contracts or unsafe workflow paths."""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _require_nonempty_string(value: Any, field: str) -> None:
    if not isinstance(value, str) or not value.strip():
        raise WorkflowError(f"{field} must be a non-empty string")


def validate_contract(data: Any) -> dict[str, Any]:
    if not isinstance(data, dict):
        raise WorkflowError("task contract must be a JSON object")
    missing = [field for field in REQUIRED_CONTRACT_FIELDS if field not in data]
    if missing:
        raise WorkflowError(f"task contract is missing required fields: {', '.join(missing)}")
    if data["schemaVersion"] != "1.0":
        raise WorkflowError("schemaVersion must be 1.0")
    _require_nonempty_string(data["runId"], "runId")
    if not RUN_ID_PATTERN.fullmatch(data["runId"]):
        raise WorkflowError("runId must contain only letters, numbers, dot, underscore, or dash")
    _require_nonempty_string(data["task"], "task")
    _require_nonempty_string(data["userObjective"], "userObjective")
    if (
        not isinstance(data["expectedDeliverables"], list)
        or not data["expectedDeliverables"]
        or any(not isinstance(item, str) or not item.strip() for item in data["expectedDeliverables"])
    ):
        raise WorkflowError("expectedDeliverables must be a non-empty string array")
    for field in ("allowedChanges", "requiredCapabilities"):
        if not isinstance(data[field], list):
            raise WorkflowError(f"{field} must be an array")
    if any(not isinstance(item, str) or not item.strip() for item in data["allowedChanges"]):
        raise WorkflowError("allowedChanges entries must be non-empty strings")
    if any(not isinstance(item, (str, dict)) for item in data["requiredCapabilities"]):
        raise WorkflowError("requiredCapabilities entries must be strings or objects")
    if not isinstance(data["acceptance"], dict):
        raise WorkflowError("acceptance must be an object")
    if not isinstance(data["authority"], dict):
        raise WorkflowError("authority must be an object")
    _require_nonempty_string(data["authority"].get("workspace"), "authority.workspace")
    if "parameters" in data and not isinstance(data["parameters"], dict):
        raise WorkflowError("parameters must be an object when present")
    if "extensions" in data and not isinstance(data["extensions"], dict):
        raise WorkflowError("extensions must be an object when present")
    return data


def load_json(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as error:
        raise WorkflowError(f"file does not exist: {path}") from error
    except json.JSONDecodeError as error:
        raise WorkflowError(f"invalid JSON in {path}: {error}") from error


def _is_relative_to(path: Path, parent: Path) -> bool:
    try:
        path.relative_to(parent)
        return True
    except ValueError:
        return False


def resolve_contract_context(
    contract_arg: str | os.PathLike[str], project_root: Path = PROJECT_ROOT
) -> tuple[Path, Path, dict[str, Any]]:
    root = project_root.resolve()
    candidate = Path(contract_arg).expanduser()
    if not candidate.is_absolute():
        candidate = root / candidate
    try:
        contract_path = candidate.resolve(strict=True)
    except FileNotFoundError as error:
        raise WorkflowError(f"contract does not exist: {candidate}") from error
    runs_root = (root / "output" / "agent-runs").resolve()
    if not _is_relative_to(contract_path, runs_root):
        raise WorkflowError("contract must stay under output/agent-runs/<run-id>/")
    run_dir = contract_path.parent
    relative = contract_path.relative_to(runs_root)
    if len(relative.parts) != 2 or relative.parts[1] != "task-contract.json":
        raise WorkflowError("contract path must be output/agent-runs/<run-id>/task-contract.json")
    data = validate_contract(load_json(contract_path))
    if data["runId"] != run_dir.name:
        raise WorkflowError("runId must match the contract directory name")
    return contract_path, run_dir, data


def resolve_run_input(run_dir: Path, raw_path: str, *, must_exist: bool = True) -> Path:
    _require_nonempty_string(raw_path, "run input path")
    candidate = Path(raw_path).expanduser()
    if not candidate.is_absolute():
        candidate = run_dir / candidate
    try:
        resolved = candidate.resolve(strict=must_exist)
    except FileNotFoundError as error:
        raise WorkflowError(f"run input does not exist: {raw_path}") from error
    if not _is_relative_to(resolved, run_dir.resolve()):
        raise WorkflowError("run input path escapes the current run directory")
    if must_exist and not resolved.is_file():
        raise WorkflowError(f"run input must be a file: {raw_path}")
    return resolved


def redact_text(text: str) -> str:
    redacted = re.sub(
        r"(?i)\b([a-z][a-z0-9+.-]*://)[^/\s:@]+:[^@\s/]+@",
        r"\1[REDACTED]@",
        text,
    )
    redacted = re.sub(
        rf"(?i)((?<!\w)--?{SENSITIVE_NAME})(=|\s+)([^\s]+)",
        r"\1\2[REDACTED]",
        redacted,
    )
    redacted = re.sub(
        rf"""(?i)((?<![\w?&]){SENSITIVE_NAME}\b\s*=\s*)([^\s"'<>]+)""",
        r"\1[REDACTED]",
        redacted,
    )
    redacted = re.sub(
        rf"""(?i)([?&]{SENSITIVE_NAME}=)[^&#\s"'<>]+""",
        r"\1[REDACTED]",
        redacted,
    )
    redacted = re.sub(
        r"(?i)(\bBearer\s+)[A-Za-z0-9._~+/=-]+",
        r"\1[REDACTED]",
        redacted,
    )
    return redacted


def redact_data(value: Any, key: str = "") -> Any:
    if key and re.search(SENSITIVE_NAME, key, flags=re.IGNORECASE):
        return "[REDACTED]" if value not in (None, "", False) else value
    if isinstance(value, dict):
        return {item_key: redact_data(item_value, str(item_key)) for item_key, item_value in value.items()}
    if isinstance(value, list):
        return [redact_data(item) for item in value]
    if isinstance(value, str):
        return redact_text(value)
    return value


def _run(
    command: list[str],
    *,
    cwd: Path = PROJECT_ROOT,
    timeout: int = 8,
) -> subprocess.CompletedProcess[str]:
    try:
        return subprocess.run(
            command,
            cwd=cwd,
            text=True,
            capture_output=True,
            check=False,
            timeout=timeout,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        return subprocess.CompletedProcess(command, 127, "", str(error))


def _first_line(value: str, limit: int = 240) -> str:
    lines = [line.strip() for line in value.splitlines() if line.strip()]
    return (lines[0] if lines else "")[:limit]


def _last_line(value: str, limit: int = 240) -> str:
    lines = [line.strip() for line in value.splitlines() if line.strip()]
    return (lines[-1] if lines else "")[:limit]


def _tool_inventory(name: str, version_args: Iterable[str] = ("--version",)) -> dict[str, Any]:
    executable = shutil.which(name)
    if not executable:
        return {"available": False}
    process = _run([executable, *version_args])
    output = _first_line(process.stdout) or _first_line(process.stderr)
    return {
        "available": True,
        "path": executable,
        "version": redact_text(output) if output else None,
        "versionExitCode": process.returncode,
    }


def _git_snapshot() -> dict[str, Any]:
    commit = _run(["git", "rev-parse", "HEAD"])
    tree = _run(["git", "rev-parse", "HEAD^{tree}"])
    branch = _run(["git", "branch", "--show-current"])
    status = _run(["git", "status", "--porcelain", "--untracked-files=normal"])
    tracked_diff = _run(["git", "diff", "--binary", "HEAD"], timeout=30)
    tracked_lines = [
        line for line in status.stdout.splitlines() if line and not line.startswith("??")
    ]
    untracked_lines = [line for line in status.stdout.splitlines() if line.startswith("??")]
    fingerprint_payload = (
        tracked_diff.stdout.encode("utf-8", errors="replace")
        + b"\0"
        + "\n".join(untracked_lines).encode("utf-8", errors="replace")
    )
    return {
        "commit": _first_line(commit.stdout) or None,
        "tree": _first_line(tree.stdout) or None,
        "branch": _first_line(branch.stdout) or None,
        "trackedChanges": len(tracked_lines),
        "untrackedFiles": len(untracked_lines),
        "worktreeFingerprint": hashlib.sha256(fingerprint_payload).hexdigest(),
    }


def _memory_bytes() -> tuple[int | None, int | None]:
    total = None
    available = None
    try:
        page_size = os.sysconf("SC_PAGE_SIZE")
        total = int(page_size * os.sysconf("SC_PHYS_PAGES"))
        if "SC_AVPHYS_PAGES" in os.sysconf_names:
            available = int(page_size * os.sysconf("SC_AVPHYS_PAGES"))
    except (OSError, ValueError):
        pass
    meminfo = Path("/proc/meminfo")
    if meminfo.is_file():
        values: dict[str, int] = {}
        for line in meminfo.read_text(encoding="utf-8").splitlines():
            key, _, value = line.partition(":")
            match = re.search(r"\d+", value)
            if match:
                values[key] = int(match.group()) * 1024
        total = values.get("MemTotal", total)
        available = values.get("MemAvailable", available)
    return total, available


def host_inventory(project_root: Path = PROJECT_ROOT) -> dict[str, Any]:
    total_memory, available_memory = _memory_bytes()
    disk = shutil.disk_usage(project_root)
    tools = {
        name: _tool_inventory(name)
        for name in ("git", "python3", "docker", "cmake", "make", "g++", "node", "npm")
    }
    return {
        "host": {
            "os": platform.system(),
            "osRelease": platform.release(),
            "architecture": platform.machine(),
            "cpuCount": os.cpu_count(),
            "memoryBytes": {"total": total_memory, "available": available_memory},
            "diskBytes": {"total": disk.total, "free": disk.free},
        },
        "repository": _git_snapshot(),
        "tools": tools,
    }


def baseline_report(project_root: Path = PROJECT_ROOT) -> dict[str, Any]:
    inventory = host_inventory(project_root)
    return {
        "schemaVersion": "1.0",
        "mode": "baseline",
        "commit": inventory["repository"]["commit"],
        "authority": {
            "scope": "read-only environment and repository inventory",
            "environmentChanges": False,
            "externalSystems": False,
        },
        **inventory,
        "unknowns": [
            "Task-specific requirements are not evaluated until a task contract is supplied.",
            "Device and production environments are not inspected by baseline mode.",
        ],
    }


def _check(
    check_id: str,
    status: str,
    detail: str,
    *,
    remediation: str = "",
    outcome: str | None = None,
    owner: str | None = None,
) -> dict[str, Any]:
    if status not in CHECK_STATUSES:
        raise WorkflowError(f"unknown check status: {status}")
    result: dict[str, Any] = {"id": check_id, "status": status, "detail": detail}
    if remediation:
        result["remediation"] = remediation
    if owner:
        result["owner"] = owner
    if outcome:
        result["_outcome"] = outcome
    return result


def environment_verdict(checks: Iterable[dict[str, Any]]) -> str:
    outcomes = {item.get("_outcome") for item in checks}
    if "unsupported" in outcomes:
        return "UNSUPPORTED"
    if "needs_environment" in outcomes:
        return "NEEDS_ENVIRONMENT"
    if "repairable" in outcomes:
        return "REPAIRABLE"
    return "READY"


def _required_tool_names(contract: dict[str, Any]) -> list[str]:
    names = []
    for entry in contract["requiredCapabilities"]:
        if isinstance(entry, str):
            names.append(entry)
        elif isinstance(entry.get("tool"), str):
            names.append(entry["tool"])
    return list(dict.fromkeys(names))


def _docker_image_identity(image: str) -> tuple[dict[str, Any] | None, str]:
    process = _run(["docker", "image", "inspect", image], timeout=15)
    if process.returncode != 0:
        return None, _first_line(process.stderr) or "image is not available locally"
    try:
        payload = json.loads(process.stdout)[0]
    except (json.JSONDecodeError, IndexError, TypeError):
        return None, "docker returned an unreadable image inspection result"
    return {
        "reference": image,
        "id": payload.get("Id"),
        "repoDigests": payload.get("RepoDigests") or [],
        "architecture": payload.get("Architecture"),
        "os": payload.get("Os"),
    }, ""


def _resolve_executable(raw_value: str) -> str | None:
    candidate = Path(raw_value).expanduser()
    if candidate.is_absolute():
        return str(candidate.absolute()) if candidate.is_file() else None
    return shutil.which(raw_value)


def _toolchain_spec(parameters: dict[str, Any]) -> tuple[dict[str, Any] | None, str]:
    raw = parameters.get("toolchain")
    if not isinstance(raw, dict):
        legacy = parameters.get("toolchainImage")
        detail = (
            "parameters.toolchainImage identifies only an execution image, not the "
            "TPU-MLIR compiler package."
            if legacy
            else "parameters.toolchain does not identify a complete TPU-MLIR compiler environment."
        )
        return None, detail
    kind = raw.get("kind")
    if kind not in TOOLCHAIN_KINDS:
        return None, "parameters.toolchain.kind must be python-package or container-image."
    package = str(raw.get("package", "tpu_mlir")).strip()
    if not TOOLCHAIN_PACKAGE_PATTERN.fullmatch(package):
        return None, "parameters.toolchain.package contains unsupported characters."
    version = raw.get("version")
    if version is not None and (not isinstance(version, str) or not version.strip()):
        return None, "parameters.toolchain.version must be a non-empty version requirement."
    normalized: dict[str, Any] = {
        "kind": kind,
        "package": package,
        "version": version.strip() if isinstance(version, str) else None,
    }
    if kind == "python-package":
        executable = raw.get("pythonExecutable")
        if not isinstance(executable, str) or not executable.strip():
            return None, "python-package toolchains require parameters.toolchain.pythonExecutable."
        normalized["pythonExecutable"] = executable.strip()
    else:
        image = raw.get("image")
        if (
            not isinstance(image, str)
            or not IMAGE_REFERENCE_PATTERN.fullmatch(image)
            or "://" in image
        ):
            return None, "container-image toolchains require a safe parameters.toolchain.image."
        normalized["image"] = image
    return normalized, ""


def _identity_digest(payload: dict[str, Any]) -> str:
    canonical = json.dumps(payload, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return "sha256:" + hashlib.sha256(canonical).hexdigest()


def _parse_toolchain_probe(
    process: subprocess.CompletedProcess[str],
    *,
    kind: str,
    image: dict[str, Any] | None = None,
) -> tuple[dict[str, Any] | None, str]:
    if process.returncode != 0:
        return None, _last_line(process.stderr) or "TPU-MLIR package inspection failed"
    try:
        payload = json.loads(process.stdout)
    except json.JSONDecodeError:
        return None, "TPU-MLIR package inspection returned unreadable output"
    if not isinstance(payload, dict):
        return None, "TPU-MLIR package inspection returned an invalid identity"
    identity: dict[str, Any] = {"kind": kind, **payload}
    if image is not None:
        identity["image"] = image
    identity["id"] = _identity_digest(identity)
    return identity, ""


def inspect_toolchain(specification: dict[str, Any]) -> tuple[dict[str, Any] | None, str]:
    package = str(specification["package"])
    expected_version = specification.get("version")
    if specification["kind"] == "python-package":
        executable = _resolve_executable(str(specification["pythonExecutable"]))
        if not executable:
            return None, (
                "declared toolchain Python is unavailable: "
                f"{specification['pythonExecutable']}"
            )
        process = _run(
            [executable, "-c", TOOLCHAIN_PROBE_SCRIPT, package],
            timeout=90,
        )
        identity, error = _parse_toolchain_probe(process, kind="python-package")
    else:
        image, image_error = _docker_image_identity(str(specification["image"]))
        if not image:
            return None, (
                f"declared toolchain image {specification['image']} is unavailable: {image_error}"
            )
        process = _run(
            [
                "docker",
                "run",
                "--rm",
                "--network",
                "none",
                "--entrypoint",
                "python3",
                str(image["id"]),
                "-c",
                TOOLCHAIN_PROBE_SCRIPT,
                package,
            ],
            timeout=120,
        )
        identity, error = _parse_toolchain_probe(
            process,
            kind="container-image",
            image=image,
        )
    if not identity:
        return None, error
    actual_version = str(identity.get("package", {}).get("version", ""))
    if not _version_satisfies(actual_version, expected_version):
        return None, (
            f"{package}={actual_version or 'unknown'} does not satisfy "
            f"{expected_version}"
        )
    return identity, ""


def _toolchain_tools_respond(identity: dict[str, Any]) -> tuple[bool, str]:
    failures = []
    for key in ("modelTransform", "modelDeploy"):
        tool = identity.get("tools", {}).get(key, {})
        path = tool.get("path")
        if not isinstance(path, str) or not path:
            failures.append(f"{key} has no executable path")
            continue
        if identity.get("kind") == "python-package":
            command = [str(identity["pythonExecutable"]), path, "--help"]
        else:
            image_id = identity.get("image", {}).get("id")
            command = [
                "docker",
                "run",
                "--rm",
                "--network",
                "none",
                "--entrypoint",
                str(identity["pythonExecutable"]),
                str(image_id),
                path,
                "--help",
            ]
        process = _run(command, timeout=120)
        output = f"{process.stdout}\n{process.stderr}".strip()
        if process.returncode not in (0, 1, 2) or not output:
            failures.append(
                f"{key} did not answer --help ({_first_line(process.stderr) or process.returncode})"
            )
    return not failures, "; ".join(failures)


def _version_tuple(value: str) -> tuple[int, ...]:
    match = re.match(r"^(\d+(?:\.\d+)*)", value)
    return tuple(int(part) for part in match.group(1).split(".")) if match else ()


def _version_satisfies(actual: str, requirement: str | None) -> bool:
    if requirement in (None, ""):
        return True
    expected = str(requirement).strip()
    if expected.startswith("=="):
        return actual == expected[2:]
    if expected.startswith(">="):
        actual_tuple = _version_tuple(actual)
        expected_tuple = _version_tuple(expected[2:])
        return bool(actual_tuple and expected_tuple and actual_tuple >= expected_tuple)
    return actual == expected


def _python_environment_check(
    executable: str, requirements: dict[str, str | None]
) -> tuple[bool, str]:
    if any(not re.fullmatch(r"[A-Za-z0-9_.-]+", name) for name in requirements):
        raise WorkflowError("parameters.pythonPackages contains an invalid package name")
    script = (
        "import importlib, importlib.metadata, json, sys;"
        "req=json.loads(sys.argv[1]); out={};"
        "\nfor name in req:"
        "\n importlib.import_module(name.replace('-', '_'));"
        "\n try: out[name]=importlib.metadata.version(name)"
        "\n except importlib.metadata.PackageNotFoundError: out[name]=getattr(importlib.import_module(name.replace('-', '_')), '__version__', 'unknown')"
        "\nprint(json.dumps(out, sort_keys=True))"
    )
    process = _run([executable, "-c", script, json.dumps(requirements)], timeout=20)
    if process.returncode != 0:
        return False, _first_line(process.stderr) or "required Python packages could not be imported"
    try:
        versions = json.loads(process.stdout)
    except json.JSONDecodeError:
        return False, "Python package version output was unreadable"
    mismatches = [
        f"{name}={versions.get(name, 'unknown')} (requires {requirement})"
        for name, requirement in requirements.items()
        if not _version_satisfies(str(versions.get(name, "unknown")), requirement)
    ]
    summary = ", ".join(f"{name}={versions.get(name, 'unknown')}" for name in sorted(versions))
    if mismatches:
        return False, "; ".join(mismatches)
    version = _run([executable, "--version"])
    python_version = _first_line(version.stdout) or _first_line(version.stderr)
    return True, f"{python_version}; {summary}"


def task_environment_report(
    task: str,
    contract_path: Path,
    run_dir: Path,
    contract: dict[str, Any],
    project_root: Path = PROJECT_ROOT,
) -> dict[str, Any]:
    if task != contract["task"]:
        raise WorkflowError("--task must match task-contract.json")
    inventory = host_inventory(project_root)
    params = contract.get("parameters", {})
    checks: list[dict[str, Any]] = []

    architecture = inventory["host"]["architecture"].lower()
    allowed_architectures = [str(value).lower() for value in params.get("hostArchitectures", [])]
    if task == "model-conversion" and not allowed_architectures:
        allowed_architectures = ["x86_64", "amd64"]
    memory_available = inventory["host"]["memoryBytes"]["available"]
    disk_free = inventory["host"]["diskBytes"]["free"]
    minimum_memory = int(params.get("minimumMemoryGiB", 0) * 1024**3)
    minimum_disk = int(params.get("minimumDiskGiB", 0) * 1024**3)
    resource_failures = []
    if allowed_architectures and architecture not in allowed_architectures:
        resource_failures.append(
            f"host architecture {architecture} is not one of {', '.join(allowed_architectures)}"
        )
    if minimum_memory and (memory_available is None or memory_available < minimum_memory):
        resource_failures.append("available memory is below the task contract requirement")
    if minimum_disk and disk_free < minimum_disk:
        resource_failures.append("free disk is below the task contract requirement")
    if resource_failures:
        checks.append(
            _check(
                "C1",
                "FAIL",
                "; ".join(resource_failures),
                remediation="Use a development machine that satisfies the recorded architecture and resource requirements.",
                outcome="needs_environment",
            )
        )
    else:
        checks.append(_check("C1", "PASS", "Host architecture and task-recorded resource requirements match."))

    repository = inventory["repository"]
    if repository["commit"] and os.access(project_root, os.R_OK | os.W_OK):
        detail = (
            f"Repository identity frozen at {repository['commit']}; "
            f"tracked change count is {repository['trackedChanges']} and "
            f"untracked file count is {repository['untrackedFiles']}."
        )
        checks.append(_check("C2", "PASS", detail))
    else:
        checks.append(
            _check(
                "C2",
                "FAIL",
                "Repository identity or requested workspace access could not be confirmed.",
                remediation="Open a writable isolated checkout and rerun the admission check.",
                outcome="needs_environment",
            )
        )

    missing_tools = []
    for name in _required_tool_names(contract):
        if name in {"docker", "python", "python3"} and task == "model-conversion":
            continue
        if not shutil.which(name):
            missing_tools.append(name)
    if missing_tools:
        checks.append(
            _check(
                "C3",
                "FAIL",
                f"Required tools are missing: {', '.join(missing_tools)}.",
                remediation="Approve a specific dependency plan or provide a machine with these tools already installed.",
                outcome="repairable",
            )
        )
    else:
        checks.append(_check("C3", "PASS", "Task-recorded host tools are available."))

    toolchain: dict[str, Any] | None = None
    if task == "model-conversion":
        source_model = params.get("sourceModel")
        target_chip = params.get("targetChip")
        _require_nonempty_string(source_model, "parameters.sourceModel")
        _require_nonempty_string(target_chip, "parameters.targetChip")
        source_path = resolve_run_input(run_dir, source_model)
        if source_path.suffix.lower() != ".onnx":
            raise WorkflowError("the first model-conversion profile requires an ONNX source model")

        toolchain_spec, specification_error = _toolchain_spec(params)
        docker_ready = False
        if toolchain_spec and toolchain_spec["kind"] == "container-image":
            docker_path = shutil.which("docker")
            if not docker_path:
                checks.append(
                    _check(
                        "C4",
                        "FAIL",
                        "The selected complete-toolchain image requires Docker, but Docker is unavailable.",
                        remediation=(
                            "Verify the selected route first, then provide Docker through the normal "
                            "IT process or select an already installed python-package toolchain."
                        ),
                        outcome="repairable",
                        owner="development-environment",
                    )
                )
            else:
                daemon = _run(
                    [docker_path, "info", "--format", "{{.ServerVersion}}"],
                    timeout=15,
                )
                if daemon.returncode == 0:
                    docker_ready = True
                    checks.append(
                        _check(
                            "C4",
                            "PASS",
                            f"Docker daemon is available ({_first_line(daemon.stdout)}).",
                        )
                    )
                else:
                    checks.append(
                        _check(
                            "C4",
                            "FAIL",
                            "Docker is installed but unavailable to the current user.",
                            remediation=(
                                "Have an authorized operator start or grant access to Docker, "
                                "then rerun doctor."
                            ),
                            outcome="repairable",
                            owner="development-environment",
                        )
                    )
        else:
            checks.append(
                _check(
                    "C4",
                    "SKIP",
                    (
                        "Docker is not required by the selected python-package toolchain."
                        if toolchain_spec
                        else "Docker cannot be selected until the complete compiler toolchain is specified."
                    ),
                )
            )

        mapping_supported = target_chip.lower() == "bm1688"
        if not mapping_supported:
            mapping_supported = bool(params.get("toolchainChip") and params.get("chipMappingEvidence"))
        if not mapping_supported:
            checks.append(
                _check(
                    "C5",
                    "UNVERIFIED",
                    f"No independent compiler/runtime mapping is recorded for target chip {target_chip}.",
                    remediation="Add measured mapping evidence for this chip; do not reuse BM1688 evidence.",
                    outcome="unsupported",
                )
            )
        elif not toolchain_spec:
            checks.append(
                _check(
                    "C5",
                    "FAIL",
                    specification_error,
                    remediation=(
                        "Correct the repository instructions or generated task contract and validate "
                        "the complete compiler path before asking the customer to change their machine."
                    ),
                    outcome="repairable",
                    owner="repository",
                )
            )
        else:
            can_inspect = (
                toolchain_spec["kind"] == "python-package"
                or (toolchain_spec["kind"] == "container-image" and docker_ready)
            )
            inspected, toolchain_error = (
                inspect_toolchain(toolchain_spec)
                if can_inspect
                else (None, "the selected execution layer is not available")
            )
            if inspected:
                tools_ready, tools_error = _toolchain_tools_respond(inspected)
                if tools_ready:
                    toolchain = inspected
                    checks.append(
                        _check(
                            "C5",
                            "PASS",
                            (
                                f"Complete TPU-MLIR toolchain is frozen as {inspected['id']} "
                                f"({inspected['package']['name']} "
                                f"{inspected['package']['version']})."
                            ),
                        )
                    )
                else:
                    checks.append(
                        _check(
                            "C5",
                            "FAIL",
                            f"TPU-MLIR package exists but its compiler commands are unusable: {tools_error}.",
                            remediation=(
                                "Repair or replace the isolated compiler environment; do not treat "
                                "package files alone as READY."
                            ),
                            outcome="repairable",
                            owner="development-environment",
                        )
                    )
            else:
                official_base_only = (
                    toolchain_spec["kind"] == "container-image"
                    and str(toolchain_spec.get("image", "")).startswith("sophgo/tpuc_dev:")
                )
                checks.append(
                    _check(
                        "C5",
                        "FAIL",
                        (
                            "The declared image is a base development environment, not a complete "
                            f"TPU-MLIR compiler: {toolchain_error}."
                            if official_base_only
                            else f"The declared TPU-MLIR toolchain is unavailable: {toolchain_error}."
                        ),
                        remediation=(
                            "Add and freeze the TPU-MLIR package in that environment or select an "
                            "existing isolated Python environment. Revalidate the repository "
                            "instructions before asking the customer to install anything."
                            if official_base_only
                            else "Verify the declared reference against upstream documentation, then "
                            "repair the isolated development environment only with explicit approval."
                        ),
                        outcome="repairable",
                        owner=(
                            "repository"
                            if official_base_only
                            else "development-environment"
                        ),
                    )
                )

        preflight = params.get("preflight", {})
        if not isinstance(preflight, dict):
            raise WorkflowError("parameters.preflight must be an object")
        python_name = str(
            preflight.get(
                "pythonExecutable",
                params.get("pythonExecutable", "python3"),
            )
        )
        python_path = shutil.which(python_name) if not Path(python_name).is_absolute() else python_name
        if not python_path or not Path(python_path).is_file():
            checks.append(
                _check(
                    "C6",
                    "FAIL",
                    f"Python executable is unavailable: {python_name}.",
                    remediation="Use an approved virtual environment with Python, onnx, onnxruntime, and numpy.",
                    outcome="repairable",
                    owner="development-environment",
                )
            )
        else:
            package_requirements = preflight.get(
                "pythonPackages",
                params.get(
                    "pythonPackages",
                    {"numpy": None, "onnx": None, "onnxruntime": None},
                ),
            )
            if not isinstance(package_requirements, dict) or any(
                requirement is not None and not isinstance(requirement, str)
                for requirement in package_requirements.values()
            ):
                raise WorkflowError(
                    "parameters.preflight.pythonPackages must map package names to versions or null"
                )
            imports_ok, detail = _python_environment_check(python_path, package_requirements)
            if imports_ok:
                checks.append(_check("C6", "PASS", f"ONNX preflight runtime is available ({detail})."))
            else:
                checks.append(
                    _check(
                        "C6",
                        "FAIL",
                        f"ONNX preflight dependencies are incomplete: {detail}.",
                        remediation="Approve an isolated venv or provide one with numpy, onnx, and onnxruntime.",
                        outcome="repairable",
                        owner="development-environment",
                    )
                )
    else:
        checks.extend(
            [
                _check("C4", "SKIP", "Docker is not required by this task profile."),
                _check("C5", "SKIP", "No model-conversion toolchain is required."),
                _check("C6", "SKIP", "No model export or ONNX preflight is required."),
            ]
        )

    requires_external = any(
        bool(params.get(field))
        for field in ("requiresNetwork", "requiresDevice", "requiresExternalWrite", "requiresGpu")
    )
    external_authority = bool(contract["authority"].get("externalSystems", False))
    if requires_external and not external_authority:
        checks.append(
            _check(
                "C7",
                "BLOCKED",
                "The task requires an external capability that is not included in the recorded authority.",
                remediation="Confirm the exact target, permission, risk, and recovery boundary separately.",
                outcome="repairable",
            )
        )
    elif requires_external:
        checks.append(_check("C7", "PASS", "Required external capability is explicitly represented in authority."))
    else:
        checks.append(_check("C7", "SKIP", "No network, device, GPU, or external write is required."))

    verdict = environment_verdict(checks)
    public_checks = [{key: value for key, value in item.items() if key != "_outcome"} for item in checks]
    return {
        "schemaVersion": "1.0",
        "commit": inventory["repository"]["commit"],
        "tree": inventory["repository"]["tree"],
        "task": task,
        "runId": contract["runId"],
        "contractSha256": sha256_file(contract_path),
        "authority": redact_data(contract["authority"]),
        "host": inventory["host"],
        "repository": inventory["repository"],
        "checks": public_checks,
        "toolchain": toolchain,
        "environmentVerdict": verdict,
    }


def atomic_write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temporary = Path(temporary_name)
    try:
        os.fchmod(descriptor, 0o600)
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            json.dump(data, stream, ensure_ascii=False, indent=2)
            stream.write("\n")
        temporary.replace(path)
    finally:
        if temporary.exists():
            temporary.unlink()


def _print_report(report: dict[str, Any], output_format: str) -> None:
    if output_format == "json":
        print(json.dumps(report, ensure_ascii=False, indent=2))
        return
    if report.get("mode") == "baseline":
        host = report["host"]
        repository = report["repository"]
        print(f"Repository: {repository.get('commit') or 'UNVERIFIED'}")
        print(f"Host: {host['os']} {host['architecture']}")
        for name, item in report["tools"].items():
            state = "PASS" if item["available"] else "UNVERIFIED"
            print(f"[{state}] {name}: {item.get('version') or 'not found'}")
        print("Task-specific readiness is not evaluated in baseline mode.")
        return
    for item in report["checks"]:
        print(f"[{item['status']}] {item['id']} {item['detail']}")
        if item.get("remediation"):
            print(f"  Remediation: {item['remediation']}")
    print(f"Environment verdict: {report['environmentVerdict']}")


def doctor_main(arguments: list[str]) -> int:
    parser = argparse.ArgumentParser(
        prog="doctor.sh",
        description="Read-only CosmoEdge development-environment inventory and task admission.",
    )
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--baseline", action="store_true")
    mode.add_argument("--task")
    parser.add_argument("--contract")
    parser.add_argument("--format", choices=("text", "json"), default="text")
    options = parser.parse_args(arguments)

    if options.baseline:
        if options.contract:
            parser.error("--contract is only valid with --task")
        _print_report(baseline_report(), options.format)
        return 0
    if not options.contract:
        parser.error("--contract is required with --task")
    contract_path, run_dir, contract = resolve_contract_context(options.contract)
    report = task_environment_report(options.task, contract_path, run_dir, contract)
    atomic_write_json(run_dir / "environment-report.json", report)
    _print_report(report, options.format)
    return 0 if report["environmentVerdict"] == "READY" else 1


def main(arguments: list[str] | None = None) -> int:
    args = list(sys.argv[1:] if arguments is None else arguments)
    if not args:
        print("usage: agent_workflow.py doctor ...", file=sys.stderr)
        return 2
    command = args.pop(0)
    try:
        if command == "doctor":
            return doctor_main(args)
        raise WorkflowError(f"unknown command: {command}")
    except WorkflowError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    os.umask(0o077)
    raise SystemExit(main())
