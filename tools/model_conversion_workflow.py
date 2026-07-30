#!/usr/bin/env python3
"""Measured ONNX-to-Sophon conversion and evidence workflow."""

from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import agent_workflow as core


PROJECT_ROOT = Path(__file__).resolve().parents[1]
MODEL_NAME_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$")
TOLERANCE_PATTERN = re.compile(r"^(?:0(?:\.\d+)?|1(?:\.0+)?),(?:0(?:\.\d+)?|1(?:\.0+)?)$")


class ExecutionFailure(RuntimeError):
    """Raised when a measured conversion command fails."""


def utc_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def _require_string(value: Any, field: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise core.WorkflowError(f"{field} must be a non-empty string")
    return value.strip()


def _validate_shapes(value: Any, field: str) -> list[list[int]]:
    if (
        not isinstance(value, list)
        or not value
        or any(
            not isinstance(shape, list)
            or not shape
            or any(not isinstance(dimension, int) or dimension <= 0 for dimension in shape)
            for shape in value
        )
    ):
        raise core.WorkflowError(f"{field} must be an array of positive-integer shape arrays")
    return value


def conversion_parameters(contract: dict[str, Any], run_dir: Path) -> dict[str, Any]:
    if contract["task"] != "model-conversion":
        raise core.WorkflowError("convert_model.sh only supports task=model-conversion")
    params = contract.get("parameters", {})
    source_model = core.resolve_run_input(
        run_dir, _require_string(params.get("sourceModel"), "parameters.sourceModel")
    )
    if source_model.suffix.lower() != ".onnx":
        raise core.WorkflowError("the first conversion profile requires an ONNX source model")
    model_name = _require_string(params.get("modelName"), "parameters.modelName")
    if not MODEL_NAME_PATTERN.fullmatch(model_name):
        raise core.WorkflowError("parameters.modelName contains unsupported characters")
    target_backend = _require_string(params.get("targetBackend"), "parameters.targetBackend")
    target_chip = _require_string(params.get("targetChip"), "parameters.targetChip").lower()
    toolchain_chip = str(params.get("toolchainChip", target_chip)).strip().lower()
    quantization = _require_string(params.get("quantization"), "parameters.quantization").upper()
    input_layout = _require_string(params.get("inputLayout"), "parameters.inputLayout").upper()
    pixel_format = _require_string(params.get("pixelFormat"), "parameters.pixelFormat").lower()
    input_shapes = _validate_shapes(params.get("inputShapes"), "parameters.inputShapes")
    output_shapes = params.get("expectedOutputShapes", [])
    if output_shapes:
        output_shapes = _validate_shapes(output_shapes, "parameters.expectedOutputShapes")
    toolchain_spec, toolchain_error = core._toolchain_spec(params)
    if not toolchain_spec:
        raise core.WorkflowError(toolchain_error)
    preflight = params.get("preflight", {})
    if not isinstance(preflight, dict):
        raise core.WorkflowError("parameters.preflight must be an object")
    tolerance = params.get("tensorTolerance")
    if tolerance is not None:
        tolerance = _require_string(tolerance, "parameters.tensorTolerance")
        if not TOLERANCE_PATTERN.fullmatch(tolerance):
            raise core.WorkflowError("parameters.tensorTolerance must look like 0.99,0.90")
    return {
        "sourceModel": source_model,
        "modelName": model_name,
        "targetBackend": target_backend,
        "targetChip": target_chip,
        "toolchainChip": toolchain_chip,
        "quantization": quantization,
        "inputLayout": input_layout,
        "pixelFormat": pixel_format,
        "inputShapes": input_shapes,
        "expectedOutputShapes": output_shapes,
        "toolchainSpec": toolchain_spec,
        "tensorTolerance": tolerance,
        "pythonExecutable": str(
            preflight.get(
                "pythonExecutable",
                params.get("pythonExecutable", "python3"),
            )
        ),
        "modelFamily": str(params.get("modelFamily", "unspecified")),
        "sourceUrl": str(params.get("sourceUrl", "")),
        "recordedBy": str(params.get("recordedBy", "")),
        "raw": params,
    }


def _read_environment_report(
    contract_path: Path, run_dir: Path, contract: dict[str, Any]
) -> dict[str, Any]:
    report_path = run_dir / "environment-report.json"
    report = core.load_json(report_path)
    if not isinstance(report, dict):
        raise core.WorkflowError("environment-report.json must be an object")
    if report.get("runId") != contract["runId"] or report.get("task") != contract["task"]:
        raise core.WorkflowError("environment report does not belong to this task contract")
    if report.get("contractSha256") != core.sha256_file(contract_path):
        raise core.WorkflowError("task contract changed after environment admission; rerun doctor")
    if report.get("environmentVerdict") != "READY":
        raise core.WorkflowError("environment admission is not READY; conversion must not start")
    admitted_repository = report.get("repository")
    if not isinstance(admitted_repository, dict):
        raise core.WorkflowError("environment report does not freeze repository working state; rerun doctor")
    current_repository = core._git_snapshot()
    for field in ("commit", "tree", "worktreeFingerprint"):
        if admitted_repository.get(field) != current_repository.get(field):
            raise core.WorkflowError(
                "repository state changed after environment admission; rerun doctor"
            )
    toolchain = report.get("toolchain")
    if not isinstance(toolchain, dict) or not toolchain.get("id"):
        raise core.WorkflowError("environment report does not freeze a complete toolchain identity")
    return report


def _read_asset_selection(run_dir: Path) -> dict[str, Any]:
    path = run_dir / "asset-selection.json"
    data = core.load_json(path)
    if not isinstance(data, dict):
        raise core.WorkflowError("asset-selection.json must be an object")
    selected = data.get("selectedAssets")
    differences = data.get("differences")
    if (
        not isinstance(selected, list)
        or not selected
        or any(not isinstance(item, str) or not item.strip() for item in selected)
    ):
        raise core.WorkflowError("asset-selection.json must list at least one selected asset")
    if not isinstance(differences, list) or any(not isinstance(item, str) for item in differences):
        raise core.WorkflowError("asset-selection.json differences must be a string array")
    return data


def _run_relative(path: Path, run_dir: Path) -> str:
    return path.resolve().relative_to(run_dir.resolve()).as_posix()


def _container_path(path: Path, run_dir: Path) -> str:
    return f"/workspace/run/{_run_relative(path, run_dir)}"


def _command_text(command: list[str], run_dir: Path) -> str:
    text = core.redact_text(shlex.join(command))
    return text.replace(str(run_dir.resolve()), "$RUN_DIR")


def _write_private_text(path: Path, value: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    path.write_text(value, encoding="utf-8")
    path.chmod(0o600)


def _run_logged(
    command: list[str],
    *,
    cwd: Path,
    log_path: Path,
    commands: list[str],
    run_dir: Path,
    timeout: int = 1800,
    environment: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    commands.append(_command_text(command, run_dir))
    try:
        process = subprocess.run(
            command,
            cwd=cwd,
            text=True,
            capture_output=True,
            check=False,
            timeout=timeout,
            env=environment,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        process = subprocess.CompletedProcess(command, 127, "", str(error))
    log = (
        f"$ {_command_text(command, run_dir)}\n"
        f"exitCode: {process.returncode}\n\n"
        f"stdout:\n{core.redact_text(process.stdout)}\n\n"
        f"stderr:\n{core.redact_text(process.stderr)}\n"
    )
    _write_private_text(log_path, log)
    return process


def _docker_base(run_dir: Path, image_id: str, entrypoint: str) -> list[str]:
    return [
        "docker",
        "run",
        "--rm",
        "--network",
        "none",
        "--user",
        f"{os.getuid()}:{os.getgid()}",
        "--env",
        "HOME=/tmp",
        "--tmpfs",
        "/tmp:rw,nosuid,nodev,exec,size=2147483648",
        "--volume",
        f"{run_dir.resolve()}:/workspace/run:rw",
        "--workdir",
        "/workspace/run/work",
        "--entrypoint",
        entrypoint,
        image_id,
    ]


def _runtime_path(path: Path, run_dir: Path, toolchain: dict[str, Any]) -> str:
    if toolchain.get("kind") == "container-image":
        return _container_path(path, run_dir)
    return str(path.resolve())


def _tool_command(
    toolchain: dict[str, Any],
    tool_key: str,
    arguments: list[str],
    *,
    run_dir: Path,
) -> list[str]:
    entry = toolchain.get("tools", {}).get(tool_key)
    if not isinstance(entry, dict) or not isinstance(entry.get("path"), str):
        raise core.WorkflowError(f"admitted toolchain has no {tool_key} command")
    python_executable = str(toolchain.get("pythonExecutable", ""))
    if not python_executable:
        raise core.WorkflowError("admitted toolchain has no Python executable")
    command = [python_executable, entry["path"], *arguments]
    if toolchain.get("kind") == "python-package":
        return command
    if toolchain.get("kind") == "container-image":
        image_id = toolchain.get("image", {}).get("id")
        if not isinstance(image_id, str) or not image_id:
            raise core.WorkflowError("admitted container toolchain has no image identity")
        return _docker_base(run_dir, image_id, python_executable) + [
            entry["path"],
            *arguments,
        ]
    raise core.WorkflowError("admitted toolchain kind is unsupported")


def _tool_help(
    toolchain: dict[str, Any],
    tool_key: str,
    *,
    run_dir: Path,
    log_path: Path,
    commands: list[str],
) -> str:
    process = _run_logged(
        _tool_command(toolchain, tool_key, ["--help"], run_dir=run_dir),
        cwd=PROJECT_ROOT,
        log_path=log_path,
        commands=commands,
        run_dir=run_dir,
        timeout=60,
        environment=core.toolchain_environment(toolchain),
    )
    return f"{process.stdout}\n{process.stderr}" if process.returncode in (0, 1, 2) else ""


def build_transform_arguments(
    parameters: dict[str, Any],
    *,
    source_model: str,
    mlir: str,
    test_input: str | None = None,
    test_result: str | None = None,
) -> list[str]:
    arguments = [
        "--model_name",
        parameters["modelName"],
        "--model_def",
        source_model,
        "--input_shapes",
        json.dumps(parameters["inputShapes"], separators=(",", ":")),
        "--pixel_format",
        parameters["pixelFormat"],
        "--mlir",
        mlir,
    ]
    if test_input and test_result:
        arguments.extend(["--test_input", test_input, "--test_result", test_result])
    return arguments


def build_deploy_arguments(
    parameters: dict[str, Any],
    *,
    mlir: str,
    model: str,
    test_input: str | None = None,
    test_reference: str | None = None,
) -> list[str]:
    arguments = [
        "--mlir",
        mlir,
        "--quantize",
        parameters["quantization"],
        "--chip",
        parameters["toolchainChip"],
        "--model",
        model,
    ]
    if test_input and test_reference:
        arguments.extend(
            [
                "--test_input",
                test_input,
                "--test_reference",
                test_reference,
                "--tolerance",
                parameters["tensorTolerance"],
            ]
        )
    return arguments


def _shape_appears(text: str, shape: list[int]) -> bool:
    compact = re.sub(r"\s+", "", text.lower())
    comma = "[" + ",".join(str(value) for value in shape) + "]"
    cross = "x".join(str(value) for value in shape)
    return comma in compact or cross in compact


def _artifact(path: Path, run_dir: Path, role: str) -> dict[str, Any]:
    return {
        "path": _run_relative(path, run_dir),
        "sha256": core.sha256_file(path),
        "sizeBytes": path.stat().st_size,
        "role": role,
    }


def _failed_manifest(
    manifest_path: Path,
    manifest: dict[str, Any],
    detail: str,
) -> None:
    manifest["status"] = "FAILED"
    manifest["completedAt"] = utc_now()
    manifest["failure"] = core.redact_text(detail)
    core.atomic_write_json(manifest_path, manifest)


def execute_conversion(
    contract_path: Path,
    run_dir: Path,
    contract: dict[str, Any],
    *,
    test_input_override: str | None = None,
) -> tuple[dict[str, Any], dict[str, Any]]:
    parameters = conversion_parameters(contract, run_dir)
    environment = _read_environment_report(contract_path, run_dir, contract)
    selection = _read_asset_selection(run_dir)
    admitted_toolchain = environment["toolchain"]
    current_toolchain, toolchain_error = core.inspect_toolchain(parameters["toolchainSpec"])
    if not current_toolchain:
        raise core.WorkflowError(
            f"admitted compiler toolchain is no longer available: {toolchain_error}"
        )
    if current_toolchain.get("id") != admitted_toolchain.get("id"):
        raise core.WorkflowError("toolchain identity changed after admission; rerun doctor")

    work_dir = run_dir / "work"
    artifacts_dir = run_dir / "artifacts"
    logs_dir = run_dir / "logs"
    verification_dir = run_dir / "verification"
    for directory in (work_dir, artifacts_dir, logs_dir, verification_dir):
        directory.mkdir(parents=True, exist_ok=True, mode=0o700)

    source_model = parameters["sourceModel"]
    model_name = parameters["modelName"]
    chip = parameters["targetChip"]
    quantization = parameters["quantization"].lower()
    mlir_path = work_dir / f"{model_name}.mlir"
    reference_path = work_dir / f"{model_name}_top_outputs.npz"
    bmodel_path = artifacts_dir / f"{model_name}_{chip}_{quantization}.bmodel"
    preflight_path = verification_dir / "onnx-check.json"
    model_info_path = verification_dir / "model-info.txt"
    manifest_path = run_dir / "execution-manifest.json"
    commands: list[str] = []
    started = time.monotonic()
    manifest: dict[str, Any] = {
        "schemaVersion": "1.0",
        "status": "RUNNING",
        "runId": contract["runId"],
        "task": contract["task"],
        "commit": environment.get("commit"),
        "tree": environment.get("tree"),
        "repository": environment["repository"],
        "contractSha256": core.sha256_file(contract_path),
        "startedAt": utc_now(),
        "source": _artifact(source_model, run_dir, "source-onnx"),
        "target": {
            "backend": parameters["targetBackend"],
            "chip": chip,
            "toolchainChip": parameters["toolchainChip"],
            "quantization": parameters["quantization"],
            "inputLayout": parameters["inputLayout"],
            "inputShapes": parameters["inputShapes"],
            "expectedOutputShapes": parameters["expectedOutputShapes"],
        },
        "toolchain": current_toolchain,
        "selectedAssets": core.redact_data(selection["selectedAssets"]),
        "assetDifferences": core.redact_data(selection["differences"]),
        "runtimeOverrides": {},
        "commands": commands,
        "stages": {},
        "artifacts": [],
    }
    core.atomic_write_json(manifest_path, manifest)

    try:
        python_name = parameters["pythonExecutable"]
        python_path = shutil.which(python_name) if not Path(python_name).is_absolute() else python_name
        if not python_path:
            raise core.WorkflowError(f"Python executable is unavailable: {python_name}")
        preflight_command = [
            str(python_path),
            str(PROJECT_ROOT / "tools" / "check_onnx_model.py"),
            str(source_model),
            "--json",
            str(preflight_path),
        ]
        shape_overrides = parameters["raw"].get("onnxInputShapeOverrides", {})
        if not isinstance(shape_overrides, dict):
            raise core.WorkflowError("parameters.onnxInputShapeOverrides must be an object")
        for input_name, shape in shape_overrides.items():
            if not isinstance(input_name, str) or not isinstance(shape, list):
                raise core.WorkflowError("invalid ONNX input shape override")
            preflight_command.extend(["--shape", f"{input_name}={','.join(map(str, shape))}"])
        preflight = _run_logged(
            preflight_command,
            cwd=PROJECT_ROOT,
            log_path=logs_dir / "S1-onnx-preflight.log",
            commands=commands,
            run_dir=run_dir,
            timeout=300,
        )
        if preflight.returncode != 0:
            manifest["stages"]["onnxPreflight"] = {
                "status": "FAIL",
                "report": _run_relative(preflight_path, run_dir) if preflight_path.exists() else None,
            }
            raise ExecutionFailure("ONNX preflight failed")
        manifest["stages"]["onnxPreflight"] = {
            "status": "PASS",
            "report": _run_relative(preflight_path, run_dir),
            "reportSha256": core.sha256_file(preflight_path),
        }
        core.atomic_write_json(manifest_path, manifest)

        transform_help = _tool_help(
            current_toolchain,
            "modelTransform",
            run_dir=run_dir,
            log_path=logs_dir / "tool-model-transform-help.log",
            commands=commands,
        )
        deploy_help = _tool_help(
            current_toolchain,
            "modelDeploy",
            run_dir=run_dir,
            log_path=logs_dir / "tool-model-deploy-help.log",
            commands=commands,
        )
        tensor_flags_supported = all(
            flag in transform_help for flag in ("--test_input", "--test_result")
        ) and all(
            flag in deploy_help
            for flag in ("--test_input", "--test_reference", "--tolerance")
        )
        manifest["toolchain"]["tensorFlagsSupported"] = tensor_flags_supported

        test_input_raw = test_input_override or parameters["raw"].get("testInput")
        test_input = (
            core.resolve_run_input(run_dir, _require_string(test_input_raw, "test input"))
            if test_input_raw
            else None
        )
        if test_input_override and test_input:
            manifest["runtimeOverrides"]["testInput"] = _run_relative(test_input, run_dir)
        tensor_requested = test_input is not None
        tensor_enabled = bool(
            tensor_requested and tensor_flags_supported and parameters["tensorTolerance"]
        )
        runtime_test_input = (
            _runtime_path(test_input, run_dir, current_toolchain)
            if tensor_enabled and test_input
            else None
        )
        runtime_reference = (
            _runtime_path(reference_path, run_dir, current_toolchain)
            if tensor_enabled
            else None
        )

        transform_args = build_transform_arguments(
            parameters,
            source_model=_runtime_path(source_model, run_dir, current_toolchain),
            mlir=_runtime_path(mlir_path, run_dir, current_toolchain),
            test_input=runtime_test_input,
            test_result=runtime_reference,
        )
        transform = _run_logged(
            _tool_command(
                current_toolchain,
                "modelTransform",
                transform_args,
                run_dir=run_dir,
            ),
            cwd=work_dir,
            log_path=logs_dir / "S2-transform.log",
            commands=commands,
            run_dir=run_dir,
            environment=core.toolchain_environment(current_toolchain),
        )
        if transform.returncode != 0 or not mlir_path.is_file():
            manifest["stages"]["transform"] = {"status": "FAIL"}
            raise ExecutionFailure("model_transform failed or did not create the requested MLIR")
        manifest["stages"]["transform"] = {
            "status": "PASS",
            "artifact": _artifact(mlir_path, run_dir, "mlir-intermediate"),
        }
        core.atomic_write_json(manifest_path, manifest)

        deploy_args = build_deploy_arguments(
            parameters,
            mlir=_runtime_path(mlir_path, run_dir, current_toolchain),
            model=_runtime_path(bmodel_path, run_dir, current_toolchain),
            test_input=runtime_test_input,
            test_reference=runtime_reference,
        )
        deploy = _run_logged(
            _tool_command(
                current_toolchain,
                "modelDeploy",
                deploy_args,
                run_dir=run_dir,
            ),
            cwd=work_dir,
            log_path=logs_dir / "S2-deploy.log",
            commands=commands,
            run_dir=run_dir,
            environment=core.toolchain_environment(current_toolchain),
        )
        if deploy.returncode != 0 or not bmodel_path.is_file():
            manifest["stages"]["deploy"] = {"status": "FAIL"}
            raise ExecutionFailure("model_deploy failed or did not create the requested bmodel")
        bmodel = _artifact(bmodel_path, run_dir, "bm1688-bmodel" if chip == "bm1688" else "bmodel")
        manifest["stages"]["deploy"] = {"status": "PASS", "artifact": bmodel}
        manifest["artifacts"] = [bmodel]

        if tensor_enabled:
            tensor_status = "PASS"
            tensor_detail = "TPU-MLIR test input/reference comparison completed during deploy."
        elif tensor_requested and not tensor_flags_supported:
            tensor_status = "UNVERIFIED"
            tensor_detail = "The admitted toolchain does not expose all required tensor comparison flags."
        elif tensor_requested and not parameters["tensorTolerance"]:
            tensor_status = "UNVERIFIED"
            tensor_detail = "A test input was supplied but no tensorTolerance was frozen in the contract."
        else:
            tensor_status = "UNVERIFIED"
            tensor_detail = "No test input was supplied for transform/deploy tensor comparison."
        manifest["stages"]["tensorCompare"] = {
            "status": tensor_status,
            "detail": tensor_detail,
            "tolerance": parameters["tensorTolerance"],
            "reference": (
                _run_relative(reference_path, run_dir) if reference_path.is_file() else None
            ),
        }

        if current_toolchain.get("tools", {}).get("modelTool"):
            model_info = _run_logged(
                _tool_command(
                    current_toolchain,
                    "modelTool",
                    [
                        "--info",
                        _runtime_path(bmodel_path, run_dir, current_toolchain),
                    ],
                    run_dir=run_dir,
                ),
                cwd=work_dir,
                log_path=logs_dir / "S2-model-info.log",
                commands=commands,
                run_dir=run_dir,
                timeout=120,
                environment=core.toolchain_environment(current_toolchain),
            )
            info_text = f"{model_info.stdout}\n{model_info.stderr}"
            if model_info.returncode == 0:
                _write_private_text(model_info_path, core.redact_text(info_text))
                expected_shapes = parameters["inputShapes"] + parameters["expectedOutputShapes"]
                contract_matches = all(_shape_appears(info_text, shape) for shape in expected_shapes)
                manifest["stages"]["modelInfo"] = {
                    "status": "PASS" if contract_matches else "UNVERIFIED",
                    "contractMatches": contract_matches,
                    "report": _run_relative(model_info_path, run_dir),
                    "reportSha256": core.sha256_file(model_info_path),
                }
            else:
                manifest["stages"]["modelInfo"] = {
                    "status": "UNVERIFIED",
                    "contractMatches": False,
                    "detail": "model_tool --info failed; see the private run log.",
                }
        else:
            manifest["stages"]["modelInfo"] = {
                "status": "UNVERIFIED",
                "contractMatches": False,
                "detail": "model_tool is unavailable in the admitted compiler toolchain.",
            }

        manifest["status"] = "COMPLETE"
        manifest["completedAt"] = utc_now()
        manifest["durationSeconds"] = round(time.monotonic() - started, 3)
        core.atomic_write_json(manifest_path, manifest)
        return manifest, parameters
    except (core.WorkflowError, ExecutionFailure) as error:
        _failed_manifest(manifest_path, manifest, str(error))
        raise


def _example_path(raw_path: str, project_root: Path = PROJECT_ROOT) -> Path:
    root = (project_root / "test" / "agent" / "examples" / "model-conversion").resolve()
    candidate = Path(raw_path).expanduser()
    if not candidate.is_absolute():
        candidate = project_root / candidate
    parent = candidate.parent.resolve()
    path = parent / candidate.name
    if not core._is_relative_to(path, root):
        raise core.WorkflowError("example record must stay under test/agent/examples/model-conversion/")
    if path.name in {"index.json", "schema.json"} or path.suffix != ".json":
        raise core.WorkflowError("example record needs a dedicated .json file")
    return path


def _recording_is_promotion_ready(
    record: dict[str, Any], manifest: dict[str, Any], parameters: dict[str, Any]
) -> bool:
    recordings = record.get("recordings", [])
    toolchain = manifest.get("toolchain", {})
    package = toolchain.get("package", {})
    tools = toolchain.get("tools", {})
    frozen_toolchain = bool(
        toolchain.get("kind") in core.TOOLCHAIN_KINDS
        and re.fullmatch(r"sha256:[0-9a-f]{64}", str(toolchain.get("id", "")))
        and package.get("name")
        and package.get("version")
        and re.fullmatch(r"[0-9a-f]{64}", str(package.get("recordSha256", "")))
        and all(
            re.fullmatch(r"[0-9a-f]{64}", str(tools.get(key, {}).get("sha256", "")))
            for key in ("modelTransform", "modelDeploy", "modelTool")
        )
        and (
            toolchain.get("kind") != "container-image"
            or toolchain.get("image", {}).get("id")
        )
    )
    return bool(
        parameters["targetChip"] == "bm1688"
        and parameters["quantization"] == "F16"
        and frozen_toolchain
        and parameters["sourceUrl"]
        and parameters["recordedBy"]
        and manifest["stages"].get("tensorCompare", {}).get("status") == "PASS"
        and manifest["stages"].get("modelInfo", {}).get("status") == "PASS"
        and len(recordings) >= 2
        and all(item.get("commit") and item.get("tree") for item in recordings)
        and len({item.get("commit") for item in recordings}) == 1
        and len({item.get("tree") for item in recordings}) == 1
        and manifest.get("repository", {}).get("trackedChanges") == 0
        and manifest.get("repository", {}).get("untrackedFiles") == 0
    )


def record_example(
    raw_path: str,
    manifest: dict[str, Any],
    parameters: dict[str, Any],
    *,
    run_dir: Path,
    project_root: Path = PROJECT_ROOT,
) -> dict[str, Any]:
    path = _example_path(raw_path, project_root)
    if not parameters["recordedBy"] or not parameters["sourceUrl"]:
        raise core.WorkflowError(
            "recording requires measured parameters.recordedBy and parameters.sourceUrl"
        )
    if core.redact_text(parameters["sourceUrl"]) != parameters["sourceUrl"]:
        raise core.WorkflowError("example sourceUrl must not contain credentials")
    bmodel = manifest["artifacts"][0]
    recording = {
        "recordedBy": parameters["recordedBy"],
        "recordedAt": manifest["completedAt"],
        "commit": manifest.get("commit"),
        "tree": manifest.get("tree"),
        "manifestSha256": core.sha256_file(run_dir / "execution-manifest.json"),
        "artifactSha256": bmodel["sha256"],
        "artifactSizeBytes": bmodel["sizeBytes"],
        "tensorTolerance": manifest["stages"]["tensorCompare"].get("tolerance"),
    }
    applicability = {
        "modelFamily": parameters["modelFamily"],
        "sourceFormat": "onnx",
        "targetBackend": parameters["targetBackend"],
        "targetChip": parameters["targetChip"],
        "inputLayout": parameters["inputLayout"],
        "quantization": parameters["quantization"],
        "inputShapes": parameters["inputShapes"],
        "expectedOutputShapes": parameters["expectedOutputShapes"],
    }
    source = {
        "model": parameters["sourceModel"].name,
        "url": parameters["sourceUrl"],
        "sha256": manifest["source"]["sha256"],
    }
    manifest_toolchain = manifest["toolchain"]
    toolchain = {
        "kind": manifest_toolchain["kind"],
        "id": manifest_toolchain["id"],
        "package": {
            key: manifest_toolchain["package"].get(key)
            for key in ("name", "version", "recordSha256")
        },
        "tools": {
            key: {"sha256": manifest_toolchain["tools"][key].get("sha256")}
            for key in ("modelTransform", "modelDeploy", "modelTool")
        },
    }
    if manifest_toolchain.get("image"):
        toolchain["image"] = {
            key: manifest_toolchain["image"].get(key)
            for key in ("reference", "id", "repoDigests")
        }
    if path.exists():
        record = core.load_json(path)
        for field, expected in (
            ("applicability", applicability),
            ("source", source),
            ("toolchain", toolchain),
        ):
            if record.get(field) != expected:
                raise core.WorkflowError(
                    f"recording does not match the existing example {field}; use a new example id"
                )
        if any(item.get("manifestSha256") == recording["manifestSha256"] for item in record["recordings"]):
            raise core.WorkflowError("this execution manifest is already recorded")
        record["recordings"].append(recording)
    else:
        record = {
            "schemaVersion": "1.0",
            "exampleId": f"model-conversion/{path.stem}",
            "task": "model-conversion",
            "status": "candidate",
            "applicability": applicability,
            "source": source,
            "toolchain": toolchain,
            "validatedStages": [
                name
                for name, stage in (
                    ("onnx-preflight", manifest["stages"].get("onnxPreflight", {})),
                    ("transform", manifest["stages"].get("transform", {})),
                    ("deploy", manifest["stages"].get("deploy", {})),
                    ("model-info", manifest["stages"].get("modelInfo", {})),
                    ("tensor-compare", manifest["stages"].get("tensorCompare", {})),
                )
                if stage.get("status") == "PASS"
            ],
            "recordings": [recording],
            "knownLimits": [
                "device import and runtime are not covered",
                "business accuracy acceptance is not covered",
            ],
        }
    record["status"] = (
        "verified" if _recording_is_promotion_ready(record, manifest, parameters) else "candidate"
    )
    serialized = json.dumps(record, ensure_ascii=False)
    if re.search(r"<[^>]+>|\b(?:TODO|TBD|placeholder)\b", serialized, flags=re.IGNORECASE):
        raise core.WorkflowError("example records cannot contain placeholders")
    core.atomic_write_json(path, record)
    path.chmod(0o644)
    return record


def _resolve_manifest_artifact(run_dir: Path, entry: dict[str, Any]) -> Path:
    raw_path = _require_string(entry.get("path"), "manifest artifact path")
    return core.resolve_run_input(run_dir, raw_path)


def _package_stage(contract: dict[str, Any], run_dir: Path, parameters: dict[str, Any]) -> dict[str, Any]:
    output_kind = str(parameters["raw"].get("outputKind", "bmodel"))
    if output_kind != "model-package":
        return {"id": "S4", "status": "SKIP", "detail": "The task requests a bmodel, not a full import package."}
    package_raw = parameters["raw"].get("packageDirectory")
    if not isinstance(package_raw, str) or not package_raw:
        return {
            "id": "S4",
            "status": "UNVERIFIED",
            "detail": "A full package was requested but parameters.packageDirectory is not recorded.",
        }
    candidate = Path(package_raw)
    if not candidate.is_absolute():
        candidate = run_dir / candidate
    try:
        package = candidate.resolve(strict=True)
    except FileNotFoundError:
        return {"id": "S4", "status": "FAIL", "detail": "The requested model package directory does not exist."}
    if not core._is_relative_to(package, run_dir.resolve()) or not package.is_dir():
        return {"id": "S4", "status": "FAIL", "detail": "The package directory escapes the current run."}
    if not re.fullmatch(r"prod_[A-Z0-9]+_[0-9]{7}_.+_V[0-9]+\.0\.[0-9]+", package.name):
        return {"id": "S4", "status": "FAIL", "detail": "The package directory name does not match the import contract."}
    config_path = package / "config.json"
    model_path = package / "model.nn"
    if not config_path.is_file() or not model_path.is_file():
        return {"id": "S4", "status": "FAIL", "detail": "The package must contain config.json and model.nn."}
    try:
        config = core.load_json(config_path)
    except core.WorkflowError as error:
        return {"id": "S4", "status": "FAIL", "detail": str(error)}
    if str(config.get("chip_type", "")).lower() != parameters["targetChip"]:
        return {"id": "S4", "status": "FAIL", "detail": "config.json chip_type differs from the task contract."}
    models = config.get("models")
    if not isinstance(models, list) or not models:
        return {"id": "S4", "status": "FAIL", "detail": "config.json models must be non-empty."}
    for model in models:
        if not isinstance(model, dict) or not model.get("inputs") or not model.get("outputs"):
            return {"id": "S4", "status": "FAIL", "detail": "Each configured model needs inputs and outputs."}
    return {
        "id": "S4",
        "status": "PASS",
        "detail": "Package name, config.json, model.nn, chip, and tensor declarations are present and self-consistent.",
    }


def _example_applicable(example: dict[str, Any], parameters: dict[str, Any]) -> bool:
    applicability = example.get("applicability", {})
    expected = {
        "modelFamily": parameters["modelFamily"],
        "sourceFormat": "onnx",
        "targetBackend": parameters["targetBackend"],
        "targetChip": parameters["targetChip"],
        "inputLayout": parameters["inputLayout"],
        "quantization": parameters["quantization"],
        "inputShapes": parameters["inputShapes"],
        "expectedOutputShapes": parameters["expectedOutputShapes"],
    }
    return all(applicability.get(key) == value for key, value in expected.items())


def verify_conversion(
    contract_path: Path,
    run_dir: Path,
    contract: dict[str, Any],
    *,
    example_path: str | None = None,
    project_root: Path = PROJECT_ROOT,
) -> dict[str, Any]:
    parameters = conversion_parameters(contract, run_dir)
    environment = _read_environment_report(contract_path, run_dir, contract)
    selection = _read_asset_selection(run_dir)
    manifest_path = run_dir / "execution-manifest.json"
    manifest = core.load_json(manifest_path)
    if not isinstance(manifest, dict) or manifest.get("status") != "COMPLETE":
        raise core.WorkflowError("execution-manifest.json is absent or not COMPLETE")
    if (
        manifest.get("runId") != contract["runId"]
        or manifest.get("contractSha256") != core.sha256_file(contract_path)
    ):
        raise core.WorkflowError("execution manifest does not match the current contract")

    stages: list[dict[str, Any]] = []
    preflight = manifest.get("stages", {}).get("onnxPreflight", {})
    try:
        preflight_path = core.resolve_run_input(run_dir, str(preflight.get("report", "")))
    except core.WorkflowError:
        preflight_path = None
    if (
        preflight.get("status") == "PASS"
        and preflight_path is not None
        and core.sha256_file(preflight_path) == preflight.get("reportSha256")
    ):
        preflight_data = core.load_json(preflight_path)
        current_source_sha256 = core.sha256_file(parameters["sourceModel"])
        source_matches = (
            preflight_data.get("model", {}).get("sha256")
            == manifest["source"]["sha256"]
            == current_source_sha256
        )
        stages.append(
            {
                "id": "S1",
                "status": "PASS" if source_matches else "FAIL",
                "detail": (
                    "ONNX checker and ONNX Runtime smoke passed for the recorded source."
                    if source_matches
                    else "ONNX preflight source identity differs from the execution manifest."
                ),
            }
        )
    else:
        stages.append({"id": "S1", "status": "FAIL", "detail": "ONNX preflight evidence is missing or changed."})

    artifact_entries = manifest.get("artifacts", [])
    artifact_failures = []
    deliverables = []
    for entry in artifact_entries if isinstance(artifact_entries, list) else []:
        try:
            path = _resolve_manifest_artifact(run_dir, entry)
        except core.WorkflowError as error:
            artifact_failures.append(str(error))
            continue
        if core.sha256_file(path) != entry.get("sha256"):
            artifact_failures.append(f"SHA-256 mismatch for {entry.get('path')}")
            continue
        deliverables.append(
            {
                "path": entry["path"],
                "sha256": entry["sha256"],
                "sizeBytes": path.stat().st_size,
                "role": entry.get("role", "artifact"),
            }
        )
    model_info = manifest.get("stages", {}).get("modelInfo", {})
    if artifact_failures or not deliverables:
        stages.append(
            {
                "id": "S2",
                "status": "FAIL",
                "detail": "; ".join(artifact_failures) or "No conversion artifact is recorded.",
            }
        )
    elif model_info.get("status") == "PASS" and model_info.get("contractMatches") is True:
        stages.append(
            {
                "id": "S2",
                "status": "PASS",
                "detail": "Artifact identity and model_tool input/output contract match the candidate task.",
            }
        )
    else:
        stages.append(
            {
                "id": "S2",
                "status": "UNVERIFIED",
                "detail": "Artifact hashes pass, but model_tool input/output matching is not fully verified.",
            }
        )

    tensor = manifest.get("stages", {}).get("tensorCompare", {})
    tensor_status = tensor.get("status") if tensor.get("status") in {"PASS", "FAIL", "UNVERIFIED"} else "UNVERIFIED"
    stages.append(
        {
            "id": "S3",
            "status": tensor_status,
            "detail": str(tensor.get("detail", "Tensor comparison evidence is unavailable.")),
        }
    )
    stages.append(_package_stage(contract, run_dir, parameters))
    stages.append(
        {
            "id": "S5",
            "status": "UNVERIFIED",
            "detail": "Device import, runtime, and business accuracy were not run by this local workflow.",
        }
    )

    required_tensor = bool(contract["acceptance"].get("requireTensorCompare", True))
    required_package = str(parameters["raw"].get("outputKind", "bmodel")) == "model-package"
    by_id = {stage["id"]: stage for stage in stages}
    if any(by_id[item]["status"] == "FAIL" for item in ("S1", "S2")):
        development_verdict = "FAILED"
    elif required_package and by_id["S4"]["status"] == "FAIL":
        development_verdict = "FAILED"
    elif (
        by_id["S1"]["status"] == "PASS"
        and by_id["S2"]["status"] == "PASS"
        and (not required_tensor or by_id["S3"]["status"] == "PASS")
        and (not required_package or by_id["S4"]["status"] == "PASS")
    ):
        development_verdict = "COMPLETE"
    elif deliverables:
        development_verdict = "PARTIAL"
    else:
        development_verdict = "UNVERIFIED"

    promotion_requested = bool(contract["acceptance"].get("promoteExample", False) or example_path)
    promotion_verdict = "NOT_REQUESTED"
    selected_example = None
    if promotion_requested:
        promotion_verdict = "NOT_READY"
        if example_path:
            path = _example_path(example_path, project_root)
            if not path.is_file():
                raise core.WorkflowError(f"example record does not exist: {path}")
            selected_example = core.load_json(path)
            if (
                selected_example.get("status") == "verified"
                and _example_applicable(selected_example, parameters)
                and all(by_id[item]["status"] == "PASS" for item in ("S1", "S2", "S3"))
                and any(
                    recording.get("manifestSha256") == core.sha256_file(manifest_path)
                    for recording in selected_example.get("recordings", [])
                )
            ):
                promotion_verdict = "READY"

    commands = [core.redact_text(str(value)) for value in manifest.get("commands", [])]
    evidence = {
        "schemaVersion": "1.0",
        "runId": contract["runId"],
        "task": contract["task"],
        "userObjective": core.redact_text(contract["userObjective"]),
        "commit": environment.get("commit"),
        "timestamp": utc_now(),
        "environmentReport": "environment-report.json",
        "contractSha256": core.sha256_file(contract_path),
        "selectedAssets": core.redact_data(selection["selectedAssets"]),
        "assetDifferences": core.redact_data(selection["differences"]),
        "selectedExample": selected_example.get("exampleId") if selected_example else None,
        "stages": stages,
        "deliverables": deliverables,
        "developmentVerdict": development_verdict,
        "promotionVerdict": promotion_verdict,
        "deviceVerdict": "NOT_RUN",
        "pendingOnDevice": [
            "model import",
            "image validation",
            "video pipeline",
            "offline business-accuracy comparison",
        ],
        "commands": commands,
    }
    core.atomic_write_json(run_dir / "evidence.json", evidence)
    lines = [
        "# Model conversion evidence",
        "",
        f"- Task result: **{development_verdict}**",
        f"- Official-example promotion: **{promotion_verdict}**",
        "- Device result: **NOT_RUN**",
        "",
        "## Layered verification",
        "",
    ]
    lines.extend(f"- {stage['id']} {stage['status']}: {stage['detail']}" for stage in stages)
    lines.extend(
        [
            "",
            "## Deliverables",
            "",
            *[
                f"- `{item['path']}` — SHA-256 `{item['sha256']}` ({item['sizeBytes']} bytes)"
                for item in deliverables
            ],
            "",
            "Local conversion evidence does not imply device import, runtime, or production acceptance.",
            "",
        ]
    )
    _write_private_text(run_dir / "evidence.md", "\n".join(lines))
    return evidence


def convert_main(arguments: list[str]) -> int:
    parser = argparse.ArgumentParser(prog="convert_model.sh")
    parser.add_argument("--contract", required=True)
    parser.add_argument("--test-input")
    parser.add_argument("--record-example")
    options = parser.parse_args(arguments)
    contract_path, run_dir, contract = core.resolve_contract_context(options.contract)
    try:
        manifest, parameters = execute_conversion(
            contract_path,
            run_dir,
            contract,
            test_input_override=options.test_input,
        )
        if options.record_example:
            record = record_example(
                options.record_example,
                manifest,
                parameters,
                run_dir=run_dir,
            )
            print(f"Example recording status: {record['status']}")
        print(f"Conversion artifact: {manifest['artifacts'][0]['path']}")
        print(f"Execution manifest: {_run_relative(run_dir / 'execution-manifest.json', run_dir)}")
        return 0
    except ExecutionFailure as error:
        print(f"conversion failed: {error}", file=sys.stderr)
        return 1


def verify_main(arguments: list[str]) -> int:
    parser = argparse.ArgumentParser(prog="verify.sh")
    parser.add_argument("--contract", required=True)
    parser.add_argument("--example")
    options = parser.parse_args(arguments)
    contract_path, run_dir, contract = core.resolve_contract_context(options.contract)
    evidence = verify_conversion(
        contract_path,
        run_dir,
        contract,
        example_path=options.example,
    )
    print(f"Development verdict: {evidence['developmentVerdict']}")
    print(f"Promotion verdict: {evidence['promotionVerdict']}")
    print("Evidence: evidence.md")
    return 0 if evidence["developmentVerdict"] == "COMPLETE" else 1


def main(arguments: list[str] | None = None) -> int:
    args = list(sys.argv[1:] if arguments is None else arguments)
    if not args:
        print("usage: model_conversion_workflow.py convert|verify ...", file=sys.stderr)
        return 2
    command = args.pop(0)
    try:
        if command == "convert":
            return convert_main(args)
        if command == "verify":
            return verify_main(args)
        raise core.WorkflowError(f"unknown command: {command}")
    except core.WorkflowError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    os.umask(0o077)
    raise SystemExit(main())
