#!/usr/bin/env python3
"""Seal and verify candidate-bound Rockchip MPP/RGA sysroots."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath
from typing import Any


MANIFEST_NAME = ".cosmo-rockchip-media.json"


class MediaSysrootError(RuntimeError):
    """Raised when a Rockchip media sysroot violates its selected lock."""


def manifest_created_at() -> str:
    source_date_epoch = os.environ.get("SOURCE_DATE_EPOCH")
    if source_date_epoch is None:
        return datetime.now(timezone.utc).isoformat()
    try:
        timestamp = int(source_date_epoch)
    except ValueError as error:
        raise MediaSysrootError("SOURCE_DATE_EPOCH must be an integer") from error
    if timestamp < 0:
        raise MediaSysrootError("SOURCE_DATE_EPOCH must not be negative")
    return datetime.fromtimestamp(timestamp, timezone.utc).isoformat()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_json(path: Path, field: str) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise MediaSysrootError(f"cannot read {field}: {path}: {error}") from error
    if not isinstance(value, dict):
        raise MediaSysrootError(f"{field} must contain a JSON object: {path}")
    return value


def safe_relative_path(raw: str, field: str) -> PurePosixPath:
    path = PurePosixPath(raw)
    if path.is_absolute() or not path.parts or ".." in path.parts:
        raise MediaSysrootError(f"{field} must be a safe relative path: {raw}")
    return path


def load_runtime(
    profile_path: Path,
) -> tuple[dict[str, Any], dict[str, Any], str, dict[str, Any]]:
    profile_path = profile_path.expanduser().resolve()
    profile = load_json(profile_path, "platform profile")
    media = profile.get("media")
    if not isinstance(media, dict):
        raise MediaSysrootError("platform profile has no media object")
    runtime_id = media.get("runtime_profile")
    lock_value = media.get("runtime_lock")
    if not isinstance(runtime_id, str) or not runtime_id:
        raise MediaSysrootError("platform profile has no media.runtime_profile")
    if not isinstance(lock_value, str) or not lock_value:
        raise MediaSysrootError("platform profile has no media.runtime_lock")

    lock_relative = Path(lock_value)
    if lock_relative.is_absolute():
        raise MediaSysrootError("media.runtime_lock must be relative to the profile")
    lock_path = profile_path.parent.joinpath(lock_relative).resolve()
    try:
        lock_path.relative_to(profile_path.parents[2])
    except (IndexError, ValueError) as error:
        raise MediaSysrootError("media.runtime_lock must stay under config/") from error
    lock = load_json(lock_path, "Rockchip media runtime lock")
    runtimes = lock.get("runtimes")
    if not isinstance(runtimes, dict) or runtime_id not in runtimes:
        raise MediaSysrootError(
            f"runtime profile {runtime_id!r} is absent from {lock_path}"
        )
    runtime = runtimes[runtime_id]
    if not isinstance(runtime, dict):
        raise MediaSysrootError(f"runtime profile {runtime_id!r} must be an object")
    return profile, media, runtime_id, runtime


def inspect_elf(path: Path) -> dict[str, str]:
    readelf = shutil.which("readelf")
    if not readelf:
        raise MediaSysrootError("readelf is required to verify Rockchip libraries")
    header = subprocess.run(
        [readelf, "-h", str(path)],
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    dynamic = subprocess.run(
        [readelf, "-d", str(path)],
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    if header.returncode or dynamic.returncode:
        detail = (header.stderr or dynamic.stderr).strip()
        raise MediaSysrootError(f"readelf rejected {path}: {detail}")
    machine_match = re.search(r"^\s*Machine:\s*(.+?)\s*$", header.stdout, re.M)
    soname_match = re.search(r"\(SONAME\).*?\[(.+?)\]", dynamic.stdout)
    if not machine_match or not soname_match:
        raise MediaSysrootError(f"cannot determine ELF machine and SONAME: {path}")
    return {
        "machine": machine_match.group(1),
        "soname": soname_match.group(1),
    }


def _inside_root(root: Path, path: Path, field: str) -> None:
    try:
        path.resolve().relative_to(root)
    except ValueError as error:
        raise MediaSysrootError(f"{field} escapes the media sysroot: {path}") from error


def inspect_artifacts(root: Path, runtime: dict[str, Any]) -> dict[str, Any]:
    artifacts = runtime.get("artifacts")
    if not isinstance(artifacts, dict) or not artifacts:
        raise MediaSysrootError("runtime profile has no artifacts")
    records: dict[str, Any] = {}
    for raw_path, expected_value in sorted(artifacts.items()):
        if not isinstance(raw_path, str) or not isinstance(expected_value, dict):
            raise MediaSysrootError("runtime artifacts must map paths to objects")
        relative = safe_relative_path(raw_path, "runtime artifact")
        path = root.joinpath(*relative.parts)
        if not path.is_file():
            raise MediaSysrootError(f"Rockchip media artifact is missing: {path}")
        _inside_root(root, path, "runtime artifact")
        record: dict[str, Any] = {
            "sha256": sha256(path),
            "size_bytes": path.stat().st_size,
        }
        expected_sha = expected_value.get("sha256")
        if expected_sha and record["sha256"] != expected_sha:
            raise MediaSysrootError(
                f"Rockchip media artifact hash mismatch: {raw_path}: "
                f"{record['sha256']} != {expected_sha}"
            )
        expected_elf = expected_value.get("elf")
        if expected_elf is not None:
            if not isinstance(expected_elf, dict):
                raise MediaSysrootError(f"ELF lock must be an object: {raw_path}")
            actual_elf = inspect_elf(path)
            for field in ("machine", "soname"):
                expected = expected_elf.get(field)
                if expected and actual_elf[field] != expected:
                    raise MediaSysrootError(
                        f"Rockchip media ELF {field} mismatch: {raw_path}: "
                        f"{actual_elf[field]} != {expected}"
                    )
            record["elf"] = actual_elf
        records[raw_path] = record

    expected_links = runtime.get("links", {})
    if not isinstance(expected_links, dict):
        raise MediaSysrootError("runtime links must be an object")
    links: dict[str, str] = {}
    for raw_path, expected_target in sorted(expected_links.items()):
        if not isinstance(raw_path, str) or not isinstance(expected_target, str):
            raise MediaSysrootError("runtime links must map paths to targets")
        relative = safe_relative_path(raw_path, "runtime link")
        safe_relative_path(expected_target, "runtime link target")
        path = root.joinpath(*relative.parts)
        if not path.is_symlink():
            raise MediaSysrootError(f"Rockchip media link is missing: {path}")
        actual_target = os.readlink(path)
        if actual_target != expected_target:
            raise MediaSysrootError(
                f"Rockchip media link mismatch: {raw_path}: "
                f"{actual_target} != {expected_target}"
            )
        _inside_root(root, path, "runtime link")
        links[raw_path] = actual_target
    return {"artifacts": records, "links": links}


def expected_source_revisions(runtime: dict[str, Any]) -> dict[str, str]:
    sources = runtime.get("sources")
    if not isinstance(sources, dict) or not sources:
        raise MediaSysrootError("runtime profile has no sources")
    revisions: dict[str, str] = {}
    for name, value in sorted(sources.items()):
        if not isinstance(name, str) or not isinstance(value, dict):
            raise MediaSysrootError("runtime sources must map names to objects")
        revision = value.get("revision")
        if not isinstance(revision, str) or not revision:
            raise MediaSysrootError(f"runtime source has no revision: {name}")
        revisions[name] = revision
    return revisions


def seal_sysroot(
    profile_path: Path, root: Path, supplied_sources: dict[str, str]
) -> Path:
    _, media, runtime_id, runtime = load_runtime(profile_path)
    if not media.get("require_sealed_sysroot"):
        raise MediaSysrootError(
            "selected platform runtime does not require a sealed sysroot"
        )
    expected_sources = expected_source_revisions(runtime)
    if supplied_sources != expected_sources:
        raise MediaSysrootError(
            f"source revisions do not match {runtime_id}: "
            f"{supplied_sources} != {expected_sources}"
        )
    root = root.expanduser().resolve()
    if not root.is_dir():
        raise MediaSysrootError(f"media sysroot does not exist: {root}")
    inspected = inspect_artifacts(root, runtime)
    manifest = {
        "schema_version": 1,
        "runtime_profile": runtime_id,
        "created_at": manifest_created_at(),
        "sources": runtime["sources"],
        **inspected,
    }
    manifest_path = root / MANIFEST_NAME
    manifest_path.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return manifest_path


def verify_sysroot(profile_path: Path, root: Path) -> dict[str, Any]:
    _, media, runtime_id, runtime = load_runtime(profile_path)
    root = root.expanduser().resolve()
    if not root.is_dir():
        raise MediaSysrootError(f"media sysroot does not exist: {root}")
    inspected = inspect_artifacts(root, runtime)
    manifest_path = root / MANIFEST_NAME
    if not manifest_path.is_file():
        if media.get("require_sealed_sysroot"):
            raise MediaSysrootError(f"sealed media manifest is missing: {manifest_path}")
        return {
            "runtime_profile": runtime_id,
            "manifest": None,
            **inspected,
        }

    manifest = load_json(manifest_path, "sealed media manifest")
    if manifest.get("runtime_profile") != runtime_id:
        raise MediaSysrootError(
            "sealed media runtime profile does not match the platform profile"
        )
    if manifest.get("sources") != runtime.get("sources"):
        raise MediaSysrootError("sealed media source identities do not match the lock")
    if manifest.get("artifacts") != inspected["artifacts"]:
        raise MediaSysrootError("sealed media artifact identities no longer match")
    if manifest.get("links") != inspected["links"]:
        raise MediaSysrootError("sealed media links no longer match")
    return {
        "runtime_profile": runtime_id,
        "manifest": manifest_path,
        **inspected,
    }


def parse_source(value: str) -> tuple[str, str]:
    name, separator, revision = value.partition("=")
    if not separator or not name or not revision or "\n" in value:
        raise argparse.ArgumentTypeError("source must be NAME=REVISION")
    return name, revision


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    for command in ("seal", "verify"):
        command_parser = subparsers.add_parser(command)
        command_parser.add_argument("--platform-profile", required=True, type=Path)
        command_parser.add_argument("--root", required=True, type=Path)
        if command == "seal":
            command_parser.add_argument(
                "--source", action="append", type=parse_source, required=True
            )
    args = parser.parse_args(argv)
    try:
        if args.command == "seal":
            supplied_sources = dict(args.source)
            if len(supplied_sources) != len(args.source):
                raise MediaSysrootError("each --source name must be unique")
            manifest_path = seal_sysroot(
                args.platform_profile, args.root, supplied_sources
            )
            print(f"SEALED {manifest_path}")
        else:
            result = verify_sysroot(args.platform_profile, args.root)
            manifest = result["manifest"] or "unsealed-legacy-runtime"
            print(
                f"PASS runtime={result['runtime_profile']} "
                f"manifest={manifest} root={args.root.resolve()}"
            )
    except MediaSysrootError as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
