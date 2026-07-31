#!/usr/bin/python3
"""Signed, journaled Cosmo release updater.

The production command line has fixed roots and fixed tool paths.  Test code may
import this file and construct :class:`ReleaseUpdater` with isolated paths, but
there is deliberately no environment-variable or command-line trust override.
"""

from __future__ import annotations

import argparse
import contextlib
import ctypes
import dataclasses
import errno
import fcntl
import hashlib
import json
import os
import re
import signal
import stat
import struct
import subprocess
import sys
import tarfile
import tempfile
import time
import unicodedata
import uuid
from pathlib import Path
from typing import Any, BinaryIO, Callable, Iterable, Iterator, Mapping, Sequence


FORMAT = "cosmo-release-compatibility-v3"
PAYLOAD_FORMAT = "cosmo-release-payload-v1"
STATE_FORMAT = "cosmo-release-state-v2"
JOURNAL_FORMAT = "cosmo-release-transaction-v2"
BOOTSTRAP_JOURNAL_FORMAT = "cosmo-release-bootstrap-transaction-v1"
PUBLICATION_FORMAT = "cosmo-release-publication-v1"
GUARD_PROFILE = "v2-only"
GUARD_ABI_CONTRACT_STATUS = "v2.3.3-stable-abi2"
GUARD_IMPLEMENTATION_MILESTONE = "single-device-certificate"
MODEL_IDENTITY_DOMAIN = b"cosmo-model-identity-v1"
CEM_V2_SOURCE_FORMATS = {
    1: "cosmo-nn-v1",
    2: "raw-bmodel",
}
CEM_V2_TARGET_PLATFORM = "sophon-bm1688-aarch64"
GUARD_SONAME = "libcosmo_model_guard.so.2"
GUARD_REAL_FILENAME = "libcosmo_model_guard.so.2.0.0"
GUARD_HEADER_PATH = "share/cosmo-model-guard/cosmo_model_guard_v2.h"
GUARD_ABI_MANIFEST_PATH = "share/cosmo-model-guard/cmg_v2_abi.json"
GUARD_DEPENDENCY_MANIFEST_PATH = "share/cosmo-model-guard/cmg_v2_dependencies.json"
RELEASE_BOOTSTRAP_PATH = "bin/cosmo-release-bootstrap"
MODEL_PROVISION_PATH = "bin/cosmo-model-provision"
DEPENDENCY_RUNTIME_BINDINGS = (
    ("openssl", "libcrypto_sha256", "lib/libcrypto.so.3"),
    ("sophon", "libbmrt_link_sha256", "lib/libbmrt.so"),
    ("sophon", "libbmrt_runtime_sha256", "lib/libbmrt.so.1.0"),
    ("sophon", "libbmlib_link_sha256", "lib/libbmlib.so"),
    ("sophon", "libbmlib_runtime_sha256", "lib/libbmlib.so.0"),
)
REQUIRED_GUARD_EXPORTS = (
    "CmgV2CloseArtifact",
    "CmgV2GetArtifactInfo",
    "CmgV2LoadSophonSegment",
    "CmgV2OpenArtifact",
)
ABI_DECLARED_GUARD_EXPORTS = (
    "CmgV2OpenArtifact",
    "CmgV2GetArtifactInfo",
    "CmgV2LoadSophonSegment",
    "CmgV2CloseArtifact",
)
FACADE_DIRECTORIES = ("bin", "files", "font", "lib", "resource", "scripts", "web")
REQUIRED_RELEASE_SCRIPTS = (
    "scripts/common.sh",
    "scripts/install.sh",
    "scripts/inte_run_start.sh",
    "scripts/release_bootstrap_backend.py",
    "scripts/release_health_check.sh",
    "scripts/release_updater.py",
    "scripts/release_updater.sh",
    "scripts/run_start.sh",
    "scripts/start.sh",
    "scripts/stop.sh",
)
SIGNED_CANDIDATE_SCRIPT_NAMES = frozenset(
    ("run_start.sh", "release_health_check.sh", "stop.sh")
)
MAX_ARCHIVE_ENTRIES = 100_000
MAX_ARCHIVE_BYTES = 128 * 1024 * 1024 * 1024
MAX_MANIFEST_BYTES = 128 * 1024
MAX_PAYLOAD_MANIFEST_BYTES = 32 * 1024 * 1024
MAX_HEALTH_SCRIPT_BYTES = 128 * 1024
MAX_GUARD_HEADER_BYTES = 128 * 1024
MAX_GUARD_ABI_MANIFEST_BYTES = 256 * 1024
MAX_GUARD_DEPENDENCY_MANIFEST_BYTES = 64 * 1024
MAX_LIBCRYPTO_BYTES = 128 * 1024 * 1024
MAX_RUNTIME_LIBRARY_BYTES = 256 * 1024 * 1024
CEM_V2_CORE_PREAMBLE_SIZE = 112
CEM_V2_MAX_CORE_BYTES = 16 * 1024 * 1024 * 1024
CEM_V2_MAX_MANIFEST_BYTES = 1024 * 1024
CEM_V2_MAX_SEGMENTS = 8
CEM_V2_MAX_CHUNKS = 65536
CEM_V2_MIN_NOMINAL_CHUNK_BYTES = 1024 * 1024
CEM_V2_MAX_CHUNK_PLAIN_BYTES = 16 * 1024 * 1024
CEM_V2_MAX_SEGMENT_PLAIN_BYTES = 1 << 31
CEM_V2_GCM_TAG_BYTES = 16
MAX_PATH_BYTES = 4096
MAX_COMPONENT_BYTES = 255
LEGACY_RESTART_TIMEOUT_SECONDS = 60
HEALTH_RUNNER_STOP_TIMEOUT_SECONDS = 10
CANDIDATE_HEALTH_TIMEOUT_SECONDS = 70
MANAGED_PROCESS_NAMES = frozenset(("cosmo-engine", "srs", "nginx"))
PUBLICATION_MARKER_PATH = "meta/release-transaction.json"
RENAME_NOREPLACE = 1
RENAME_EXCHANGE = 2
AT_FDCWD = -100


def _is_preset_model_payload_path(path: str) -> bool:
    """Return whether *path* is a packaged preset ``model.nn`` resource."""
    return path.startswith("resource/models/") and path.endswith("/model.nn")


def _is_device_authorization_state(path: str) -> bool:
    return Path(path.lower()).name == "device-certificate.bin"


class ReleaseError(RuntimeError):
    """A fail-closed release validation or transaction error."""


class InjectedInterruption(RuntimeError):
    """Test-only simulated power loss."""


@dataclasses.dataclass(frozen=True)
class ReleasePaths:
    install_root: Path
    model_guard_state_root: Path
    openssl: Path = Path("/usr/bin/openssl")

    @property
    def releases(self) -> Path:
        return self.install_root / ".releases"

    @property
    def current(self) -> Path:
        return self.install_root / "current"

    @property
    def state_dir(self) -> Path:
        return self.install_root / ".release-state"

    @property
    def state_file(self) -> Path:
        return self.state_dir / "compatibility.state.json"

    @property
    def journal_file(self) -> Path:
        return self.state_dir / "transaction.json"

    @property
    def transactions(self) -> Path:
        return self.state_dir / "transactions"

    @property
    def lock_file(self) -> Path:
        return self.state_dir / "update.lock"

    @property
    def bootstrap_journal_file(self) -> Path:
        return self.state_dir / "bootstrap-transaction.json"

    @property
    def legacy_backup(self) -> Path:
        return self.state_dir / "legacy-layout"

    @property
    def stable_health_script(self) -> Path:
        return (
            self.install_root
            / ".release-bootstrap"
            / "scripts"
            / "release_health_check.sh"
        )


PRODUCTION_PATHS = ReleasePaths(
    install_root=Path("/appfs/cosmo_wander/cwai_data"),
    model_guard_state_root=Path("/data/cwaiuserdata/model-guard"),
)


def _fail(message: str) -> "NoReturn":
    raise ReleaseError(message)


def _sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    fd = os.open(path, os.O_RDONLY | os.O_CLOEXEC)
    try:
        with os.fdopen(fd, "rb", closefd=False) as stream:
            while True:
                block = stream.read(1024 * 1024)
                if not block:
                    break
                digest.update(block)
    finally:
        os.close(fd)
    return digest.hexdigest()


def _canonical_json(value: Any) -> bytes:
    return (
        json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")) + "\n"
    ).encode("utf-8")


def _canonical_ascii_json(value: Any) -> bytes:
    return (
        json.dumps(value, ensure_ascii=True, sort_keys=True, separators=(",", ":")) + "\n"
    ).encode("ascii")


def _publication_marker_bytes(
    transaction_id: str,
    release_id: str,
    manifest_sha256: str,
    archive_sha256: str,
) -> bytes:
    return _canonical_json(
        {
            "archive_sha256": archive_sha256,
            "format": PUBLICATION_FORMAT,
            "manifest_sha256": manifest_sha256,
            "release_id": release_id,
            "transaction_id": transaction_id,
        }
    )


def _strict_json(data: bytes, maximum: int, description: str, *, ascii_canonical: bool = False) -> Any:
    if not data or len(data) > maximum:
        _fail(f"{description} has an invalid size")

    def object_pairs(pairs: Sequence[tuple[str, Any]]) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for key, value in pairs:
            if key in result:
                _fail(f"{description} contains duplicate JSON key {key!r}")
            result[key] = value
        return result

    try:
        value = json.loads(
            data.decode("utf-8", "strict"),
            object_pairs_hook=object_pairs,
            parse_constant=lambda token: _fail(f"{description} contains {token}"),
        )
    except (UnicodeError, json.JSONDecodeError) as error:
        raise ReleaseError(f"{description} is not canonical JSON") from error
    canonical = _canonical_ascii_json(value) if ascii_canonical else _canonical_json(value)
    if canonical != data:
        _fail(f"{description} is not in canonical encoding")
    return value


def _strict_pretty_ascii_json(data: bytes, maximum: int, description: str) -> Any:
    """Parse the frozen, human-reviewed ABI JSON without parser ambiguity."""
    if not data or len(data) > maximum:
        _fail(f"{description} has an invalid size")

    def object_pairs(pairs: Sequence[tuple[str, Any]]) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for key, value in pairs:
            if key in result:
                _fail(f"{description} contains duplicate JSON key {key!r}")
            result[key] = value
        return result

    try:
        value = json.loads(
            data.decode("ascii", "strict"),
            object_pairs_hook=object_pairs,
            parse_constant=lambda token: _fail(f"{description} contains {token}"),
        )
    except (UnicodeError, json.JSONDecodeError) as error:
        raise ReleaseError(f"{description} is not canonical JSON") from error
    canonical = (json.dumps(value, ensure_ascii=True, indent=2) + "\n").encode("ascii")
    if canonical != data:
        _fail(f"{description} is not in canonical encoding")
    return value


def _require_object(value: Any, keys: set[str], description: str) -> Mapping[str, Any]:
    if not isinstance(value, dict) or set(value) != keys:
        _fail(f"{description} has an invalid schema")
    return value


def _require_string(value: Any, description: str, pattern: str | None = None) -> str:
    if not isinstance(value, str):
        _fail(f"{description} must be a string")
    if pattern is not None and re.fullmatch(pattern, value) is None:
        _fail(f"{description} has an invalid value")
    return value


def _require_uint(value: Any, description: str, minimum: int = 0, maximum: int = (1 << 63) - 1) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < minimum or value > maximum:
        _fail(f"{description} has an invalid integer value")
    return value


def _require_hex(value: Any, length: int, description: str, nonzero: bool = False) -> str:
    text = _require_string(value, description, rf"[0-9a-f]{{{length}}}")
    if nonzero and set(text) == {"0"}:
        _fail(f"{description} must be nonzero")
    return text


def _single_version_define(
    header: bytes, name: str, value_pattern: str, description: str
) -> str:
    try:
        text = header.decode("ascii", "strict")
    except UnicodeError as error:
        raise ReleaseError(f"{description} is not ASCII") from error
    text = text.replace("\r\n", "\n")
    if "\r" in text:
        _fail(f"{description} contains an invalid line ending")
    matches = re.findall(
        rf"^[ \t]*#[ \t]*define[ \t]+{re.escape(name)}[ \t]+"
        rf"({value_pattern})[ \t]*(?:/\*.*\*/)?$",
        text,
        re.MULTILINE,
    )
    if len(matches) != 1:
        _fail(f"{description} must define {name} exactly once")
    return matches[0]


def _openssl_version_from_header(header: bytes) -> str:
    major = int(
        _single_version_define(
            header, "OPENSSL_VERSION_MAJOR", r"[0-9]+", "OpenSSL version header"
        )
    )
    minor = int(
        _single_version_define(
            header, "OPENSSL_VERSION_MINOR", r"[0-9]+", "OpenSSL version header"
        )
    )
    patch = int(
        _single_version_define(
            header, "OPENSSL_VERSION_PATCH", r"[0-9]+", "OpenSSL version header"
        )
    )
    quoted = _single_version_define(
        header,
        "OPENSSL_VERSION_STR",
        r'"[0-9]+\.[0-9]+\.[0-9]+"',
        "OpenSSL version header",
    )
    version = f"{major}.{minor}.{patch}"
    if quoted != f'"{version}"' or major < 3:
        _fail("OpenSSL version header is incompatible")
    return version


def _openssl_version_from_library(library: bytes) -> str:
    versions = {
        match.decode("ascii")
        for match in re.findall(
            rb"OpenSSL ([0-9]+\.[0-9]+\.[0-9]+)(?:[ -])", library
        )
    }
    if len(versions) != 1:
        _fail("libcrypto embedded OpenSSL version banner is missing or ambiguous")
    return next(iter(versions))


def _validate_model_guard_header(header: bytes) -> None:
    if not header or len(header) > MAX_GUARD_HEADER_BYTES or b"\x00" in header:
        _fail("Model Guard v2 header size/content rejected")
    try:
        text = header.decode("utf-8", "strict")
    except UnicodeError as error:
        raise ReleaseError("Model Guard v2 header is not UTF-8") from error
    required_lines = {
        "#define CMG_V2_ABI_MAJOR UINT32_C(2)",
        "#define CMG_V2_ARTIFACT_INFO_SIZE UINT32_C(72)",
        "#define CMG_V2_SOPHON_LOAD_OPTIONS_SIZE UINT32_C(16)",
    }
    if not required_lines.issubset(set(text.splitlines())):
        _fail("Model Guard v2 header constants do not match the frozen ABI")
    for function in REQUIRED_GUARD_EXPORTS:
        if len(re.findall(rf"\b{re.escape(function)}\s*\(", text)) != 1:
            _fail(f"Model Guard v2 header declaration rejected: {function}")


def _validate_model_guard_abi_manifest(
    data: bytes,
    expected_profile: str,
) -> Mapping[str, Any]:
    value = _strict_pretty_ascii_json(
        data, MAX_GUARD_ABI_MANIFEST_BYTES, "Model Guard ABI manifest"
    )
    root = _require_object(
        value,
        {
            "abi",
            "compatibility_rules",
            "constants",
            "contract_status",
            "header",
            "implementation_milestone",
            "name",
            "runtime_profile",
            "runtime_implemented",
            "schema",
            "schema_version",
            "statuses",
            "structs",
            "symbols",
            "visibility",
        },
        "Model Guard ABI manifest",
    )
    abi = _require_object(
        root["abi"],
        {
            "calling_convention",
            "language_compatibility",
            "load_flags_type",
            "major",
            "public_sophon_header",
            "soname",
            "sophon_target",
            "source_format_type",
            "status_type",
            "target",
            "target_architecture",
        },
        "Model Guard ABI descriptor",
    )
    visibility = _require_object(
        root["visibility"],
        {
            "all_other_symbols",
            "default_visibility",
            "exclude_static_dependency_symbols",
            "global_function_allowlist",
            "version_node",
            "version_script_required",
        },
        "Model Guard ABI visibility",
    )
    runtime_profile = _require_object(
        root["runtime_profile"],
        {
            "authorization_artifact",
            "binding_profile",
            "name",
            "preset_scope",
        },
        "Model Guard runtime profile",
    )
    if expected_profile != GUARD_PROFILE:
        _fail("expected Model Guard ABI profile is invalid")
    expected_runtime_profile = {
        "authorization_artifact": "device-certificate.bin",
        "binding_profile": "software-bound-device-certificate-v1",
        "name": expected_profile,
        "preset_scope": "all-current-and-future-presets-under-one-pmk",
    }
    if (
        root["schema"] != "cosmo.model-guard.abi"
        or root["schema_version"] != 1
        or root["name"] != "cosmo-model-guard"
        or root["header"] != "include/cosmo_model_guard_v2.h"
        or root["contract_status"] != GUARD_ABI_CONTRACT_STATUS
        or root["runtime_implemented"] is not True
        or root["implementation_milestone"] != GUARD_IMPLEMENTATION_MILESTONE
        or abi["major"] != 2
        or abi["calling_convention"] != "C"
        or abi["target"] != "aarch64-linux-gnu"
        or abi["target_architecture"] != "AArch64"
        or abi["sophon_target"] != "BM1688"
        or abi["soname"] != GUARD_SONAME
        or abi["public_sophon_header"] != "bmlib_runtime.h"
        or visibility["default_visibility"] != "hidden"
        or visibility["version_script_required"] is not True
        or visibility["version_node"] != "CMG_2.0"
        or visibility["global_function_allowlist"]
        != list(ABI_DECLARED_GUARD_EXPORTS)
        or visibility["all_other_symbols"] != "local"
        or visibility["exclude_static_dependency_symbols"] is not True
        or runtime_profile != expected_runtime_profile
    ):
        _fail(
            "Model Guard ABI manifest does not describe the supported "
            "BM1688 v2.3 contract"
        )
    return root


def _validate_model_guard_dependencies(
    data: bytes,
    header: bytes,
    abi_manifest: bytes,
    expected_profile: str,
) -> Mapping[str, Any]:
    value = _strict_json(
        data,
        MAX_GUARD_DEPENDENCY_MANIFEST_BYTES,
        "Model Guard dependency manifest",
        ascii_canonical=True,
    )
    root = _require_object(
        value,
        {
            "abi",
            "guard_profile",
            "openssl",
            "schema",
            "schema_version",
            "sophon",
            "target",
        },
        "Model Guard dependency manifest",
    )
    abi = _require_object(
        root["abi"],
        {"header_path", "header_sha256", "manifest_path", "manifest_sha256"},
        "Model Guard dependency ABI binding",
    )
    openssl = _require_object(
        root["openssl"],
        {
            "headers_path",
            "headers_file_count",
            "headers_sha256",
            "version",
            "version_header_sha256",
            "libcrypto_path",
            "libcrypto_soname",
            "libcrypto_sha256",
        },
        "Model Guard OpenSSL dependency",
    )
    sophon = _require_object(
        root["sophon"],
        {
            "headers_path",
            "headers_file_count",
            "headers_sha256",
            "libraries_path",
            "libraries_file_count",
            "libraries_sha256",
            "libbmrt_link_path",
            "libbmrt_link_sha256",
            "libbmrt_runtime_path",
            "libbmrt_runtime_sha256",
            "libbmlib_link_path",
            "libbmlib_link_sha256",
            "libbmlib_runtime_path",
            "libbmlib_runtime_sha256",
        },
        "Model Guard Sophon dependency",
    )
    if expected_profile != GUARD_PROFILE:
        _fail("expected Model Guard dependency profile is invalid")
    if (
        root["schema"] != "cosmo.model-guard.dependencies"
        or root["schema_version"] != 3
        or root["target"] != "aarch64-linux-gnu"
        or root["guard_profile"] != expected_profile
        or abi["header_path"] != "include/cosmo_model_guard_v2.h"
        or abi["manifest_path"] != GUARD_ABI_MANIFEST_PATH
        or openssl["headers_path"] != "thirdparty/openssl/include"
        or openssl["libcrypto_path"]
        != "thirdparty/openssl/lib/libcrypto.so.3"
        or openssl["libcrypto_soname"] != "libcrypto.so.3"
        or re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", str(openssl["version"])) is None
        or sophon["headers_path"] != "thirdparty/sophon/include"
        or sophon["libraries_path"] != "thirdparty/sophon/lib"
        or sophon["libbmrt_link_path"] != "thirdparty/sophon/lib/libbmrt.so"
        or sophon["libbmrt_runtime_path"]
        != "thirdparty/sophon/lib/libbmrt.so.1.0"
        or sophon["libbmlib_link_path"] != "thirdparty/sophon/lib/libbmlib.so"
        or sophon["libbmlib_runtime_path"]
        != "thirdparty/sophon/lib/libbmlib.so.0"
    ):
        _fail("Model Guard dependency manifest contract rejected")
    openssl_version = tuple(int(part) for part in openssl["version"].split("."))
    if openssl_version[0] < 3:
        _fail("Model Guard OpenSSL dependency version is incompatible")
    for value_to_check, description in (
        (abi["header_sha256"], "dependency ABI header sha256"),
        (abi["manifest_sha256"], "dependency ABI manifest sha256"),
        (openssl["headers_sha256"], "OpenSSL headers sha256"),
        (openssl["version_header_sha256"], "OpenSSL version-header sha256"),
        (openssl["libcrypto_sha256"], "libcrypto sha256"),
        (sophon["headers_sha256"], "Sophon headers sha256"),
        (sophon["libraries_sha256"], "Sophon libraries sha256"),
        (sophon["libbmrt_link_sha256"], "Sophon libbmrt link sha256"),
        (sophon["libbmrt_runtime_sha256"], "Sophon libbmrt runtime sha256"),
        (sophon["libbmlib_link_sha256"], "Sophon libbmlib link sha256"),
        (sophon["libbmlib_runtime_sha256"], "Sophon libbmlib runtime sha256"),
    ):
        _require_hex(value_to_check, 64, description, nonzero=True)
    for value_to_check, description in (
        (openssl["headers_file_count"], "OpenSSL headers file count"),
        (sophon["headers_file_count"], "Sophon headers file count"),
        (sophon["libraries_file_count"], "Sophon libraries file count"),
    ):
        _require_uint(value_to_check, description, 1)
    if abi["header_sha256"] != _sha256_bytes(header):
        _fail("Model Guard dependency manifest header digest mismatch")
    if abi["manifest_sha256"] != _sha256_bytes(abi_manifest):
        _fail("Model Guard dependency manifest ABI digest mismatch")
    return root


def _canonical_relative_path(value: Any, description: str = "path") -> str:
    path = _require_string(value, description)
    if not path or path.startswith("/") or "\\" in path or unicodedata.normalize("NFC", path) != path:
        _fail(f"{description} is not canonical")
    try:
        encoded = path.encode("utf-8", "strict")
    except UnicodeError as error:
        raise ReleaseError(f"{description} is not valid UTF-8") from error
    if len(encoded) > MAX_PATH_BYTES:
        _fail(f"{description} is too long")
    components = path.split("/")
    if any(
        component in ("", ".", "..")
        or len(component.encode("utf-8")) > MAX_COMPONENT_BYTES
        or any(ord(character) < 0x20 or ord(character) == 0x7F for character in component)
        for component in components
    ):
        _fail(f"{description} contains an unsafe component")
    return path


def _safe_symlink_target(path: str, target_value: Any) -> str:
    target = _canonical_relative_path(target_value, f"symlink target for {path}")
    if target.startswith("/"):
        _fail(f"symlink target for {path} is absolute")
    parent = Path(path).parent
    resolved = Path(os.path.normpath(str(parent / target)))
    resolved_text = resolved.as_posix()
    _canonical_relative_path(resolved_text, f"resolved symlink target for {path}")
    return target


def _validate_release_id(value: Any) -> str:
    return _require_string(value, "release_id", r"[a-z0-9][a-z0-9._-]{0,63}")


def _regular_file(path: Path, maximum: int | None = None) -> os.stat_result:
    try:
        info = os.stat(path)
    except OSError as error:
        raise ReleaseError(f"required file is unavailable: {path}") from error
    if not stat.S_ISREG(info.st_mode):
        _fail(f"required path is not a regular file: {path}")
    if maximum is not None and info.st_size > maximum:
        _fail(f"file too large: {path}")
    return info


def _directory(path: Path, create: bool = False, mode: int = 0o700) -> os.stat_result:
    if create:
        path.mkdir(mode=mode, parents=True, exist_ok=True)
    try:
        info = os.stat(path)
    except OSError as error:
        raise ReleaseError(f"required directory is unavailable: {path}") from error
    if not stat.S_ISDIR(info.st_mode):
        _fail(f"required path is not a directory: {path}")
    return info


def _read_with_digest(
    path: Path,
    maximum: int,
    *,
    retain_data: bool,
) -> tuple[bytes | None, str]:
    info = _regular_file(path, maximum)
    fd = os.open(path, os.O_RDONLY | os.O_CLOEXEC)
    try:
        current = os.fstat(fd)
        if not _same_file_snapshot(info, current):
            _fail(f"file changed while opening: {path}")
        digest = hashlib.sha256()
        data = bytearray() if retain_data else None
        total = 0
        while total <= maximum:
            block = os.read(fd, min(1024 * 1024, maximum + 1 - total))
            if not block:
                break
            digest.update(block)
            total += len(block)
            if data is not None:
                data.extend(block)
        current = os.fstat(fd)
        try:
            path_current = os.stat(path)
        except OSError as error:
            raise ReleaseError(f"file changed while reading: {path}") from error
        if (
            total != info.st_size
            or total > maximum
            or not _same_file_snapshot(info, current)
            or not _same_file_snapshot(info, path_current)
        ):
            _fail(f"file changed or exceeded limit: {path}")
        return (bytes(data) if data is not None else None), digest.hexdigest()
    finally:
        os.close(fd)


def _read_exact(path: Path, maximum: int) -> bytes:
    data, _ = _read_with_digest(path, maximum, retain_data=True)
    if data is None:
        _fail("internal secure-read invariant failed")
    return data


def _sha256_limited(path: Path, maximum: int) -> str:
    _, digest = _read_with_digest(path, maximum, retain_data=False)
    return digest


def _atomic_write(path: Path, data: bytes, mode: int) -> None:
    _directory(path.parent)
    name = f".{path.name}.tmp-{uuid.uuid4().hex}"
    temporary = path.parent / name
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC | os.O_NOFOLLOW
    fd = os.open(temporary, flags, mode)
    try:
        offset = 0
        while offset < len(data):
            offset += os.write(fd, data[offset:])
        os.fdatasync(fd)
        current = os.fstat(fd)
        if not stat.S_ISREG(current.st_mode):
            _fail(f"temporary state file rejected: {temporary}")
        os.fchmod(fd, mode)
    except BaseException:
        with contextlib.suppress(OSError):
            os.unlink(temporary)
        raise
    finally:
        os.close(fd)
    os.replace(temporary, path)
    _fsync_directory(path.parent)


def _fsync_directory(path: Path) -> None:
    fd = os.open(path, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def _rename_noreplace(source: Path, destination: Path) -> None:
    libc = ctypes.CDLL(None, use_errno=True)
    renameat2 = getattr(libc, "renameat2", None)
    if renameat2 is None:
        if destination.exists() or destination.is_symlink():
            raise FileExistsError(errno.EEXIST, "destination exists", str(destination))
        os.rename(source, destination)
        return
    result = renameat2(
        ctypes.c_int(AT_FDCWD),
        ctypes.c_char_p(os.fsencode(source)),
        ctypes.c_int(AT_FDCWD),
        ctypes.c_char_p(os.fsencode(destination)),
        ctypes.c_uint(RENAME_NOREPLACE),
    )
    if result != 0:
        error_number = ctypes.get_errno()
        raise OSError(error_number, os.strerror(error_number), str(destination))


def _rename_exchange(first: Path, second: Path) -> None:
    """Atomically exchange two existing directory entries.

    First-release facade migration relies on this primitive to ensure the
    historical systemd entry path is never absent, even if power is lost at an
    instruction boundary.  There is deliberately no non-atomic fallback.
    """
    libc = ctypes.CDLL(None, use_errno=True)
    renameat2 = getattr(libc, "renameat2", None)
    if renameat2 is None:
        _fail("atomic facade exchange is unavailable")
    result = renameat2(
        ctypes.c_int(AT_FDCWD),
        ctypes.c_char_p(os.fsencode(first)),
        ctypes.c_int(AT_FDCWD),
        ctypes.c_char_p(os.fsencode(second)),
        ctypes.c_uint(RENAME_EXCHANGE),
    )
    if result != 0:
        error_number = ctypes.get_errno()
        raise ReleaseError("atomic facade exchange failed") from OSError(
            error_number, os.strerror(error_number)
        )


def _remove_private_tree(path: Path, required_parent: Path) -> None:
    """Remove one explicit transaction/release tree without following links."""
    parent = path.parent
    if parent != required_parent or path.name in ("", ".", ".."):
        _fail("refusing to remove a path outside the controlled parent")
    try:
        root_info = os.lstat(path)
    except FileNotFoundError:
        return
    if not stat.S_ISDIR(root_info.st_mode):
        _fail(f"refusing to remove unexpected tree: {path}")
    for current_root, directories, files in os.walk(path, topdown=False, followlinks=False):
        current = Path(current_root)
        for name in files:
            candidate = current / name
            info = os.lstat(candidate)
            if stat.S_ISDIR(info.st_mode):
                _fail(f"unexpected object in controlled tree: {candidate}")
            os.unlink(candidate)
        for name in directories:
            candidate = current / name
            info = os.lstat(candidate)
            if stat.S_ISLNK(info.st_mode):
                os.unlink(candidate)
            elif stat.S_ISDIR(info.st_mode):
                os.rmdir(candidate)
            else:
                _fail(f"unexpected object in controlled tree: {candidate}")
    os.rmdir(path)
    _fsync_directory(required_parent)


def _run_tool(arguments: Sequence[str], input_data: bytes | None = None) -> bytes:
    try:
        result = subprocess.run(
            list(arguments),
            input=input_data,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            env={"LC_ALL": "C", "PATH": "/usr/bin:/bin"},
            close_fds=True,
            timeout=30,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise ReleaseError(f"required validation tool failed: {arguments[0]}") from error
    if result.returncode != 0:
        _fail(f"validation tool rejected input: {Path(arguments[0]).name}")
    return result.stdout


def _validate_executable_tool(path: Path) -> None:
    if not path.is_absolute():
        _fail("validation tool path must be absolute")
    try:
        info = os.stat(path)
    except OSError as error:
        raise ReleaseError(f"required validation tool is unavailable: {path}") from error
    if (
        not stat.S_ISREG(info.st_mode)
        or not stat.S_IMODE(info.st_mode) & 0o111
    ):
        _fail(f"validation tool type or executable mode rejected: {path}")


def _validate_root_owned_tool(path: Path) -> None:
    _validate_executable_tool(path)


def _verify_ed25519(openssl: Path, public_key: Path, message: Path, signature: Path) -> None:
    _validate_executable_tool(openssl)
    _run_tool(
        (
            str(openssl),
            "pkeyutl",
            "-verify",
            "-pubin",
            "-inkey",
            str(public_key),
            "-rawin",
            "-in",
            str(message),
            "-sigfile",
            str(signature),
        )
    )


def _validate_payload_manifest(value: Any) -> tuple[Mapping[str, Any], ...]:
    root = _require_object(value, {"entries", "format"}, "payload manifest")
    if root["format"] != PAYLOAD_FORMAT or not isinstance(root["entries"], list):
        _fail("payload manifest header rejected")
    entries: list[Mapping[str, Any]] = []
    previous: bytes | None = None
    seen: set[str] = set()
    types: dict[str, str] = {}
    symlink_targets: dict[str, str] = {}
    for index, raw_entry in enumerate(root["entries"]):
        if not isinstance(raw_entry, dict) or "type" not in raw_entry:
            _fail(f"payload entry {index} is invalid")
        entry_type = raw_entry["type"]
        if entry_type == "file":
            entry = _require_object(raw_entry, {"mode", "path", "sha256", "size", "type"}, "file entry")
            _require_uint(entry["mode"], "file mode", 0, 0o777)
            _require_uint(entry["size"], "file size", 0, MAX_ARCHIVE_BYTES)
            _require_hex(entry["sha256"], 64, "file sha256")
        elif entry_type == "directory":
            entry = _require_object(raw_entry, {"mode", "path", "type"}, "directory entry")
            _require_uint(entry["mode"], "directory mode", 0, 0o777)
        elif entry_type == "symlink":
            entry = _require_object(raw_entry, {"path", "target", "type"}, "symlink entry")
        else:
            _fail("payload entry type rejected")
        path = _canonical_relative_path(entry["path"], "payload path")
        if path == "meta" or path.startswith("meta/"):
            _fail("payload may not create release metadata")
        if _is_device_authorization_state(path):
            _fail(
                "payload contains device-specific Model Guard state: "
                f"{path}"
            )
        encoded = path.encode("utf-8")
        if previous is not None and encoded <= previous:
            _fail("payload entries are not uniquely byte-sorted")
        previous = encoded
        if path in seen:
            _fail("duplicate payload path")
        seen.add(path)
        types[path] = entry_type
        if entry_type == "symlink":
            symlink_targets[path] = _safe_symlink_target(path, entry["target"])
        entries.append(entry)
    for entry in entries:
        path = str(entry["path"])
        parent = Path(path).parent
        while parent != Path("."):
            if types.get(parent.as_posix()) != "directory":
                _fail(f"payload parent directory is not declared: {parent}")
            parent = parent.parent
        if entry["type"] == "symlink":
            current = path
            visited: set[str] = set()
            while True:
                if current in visited:
                    _fail(f"payload symlink cycle rejected: {path}")
                visited.add(current)
                target = symlink_targets.get(current)
                if target is None:
                    _fail(f"symlink target is not a declared regular file: {path}")
                resolved = Path(os.path.normpath(str(Path(current).parent / target))).as_posix()
                resolved_type = types.get(resolved)
                if resolved_type == "file":
                    break
                if resolved_type != "symlink":
                    _fail(f"symlink target is not a declared regular file: {path}")
                current = resolved
    return tuple(entries)


def _validate_compatibility_manifest(value: Any) -> Mapping[str, Any]:
    """Validate the single-certificate v3 schema for every release."""
    root = _require_object(
        value,
        {
            "edge",
            "device_certificate_schema",
            "format",
            "model_guard",
            "payload_manifest_sha256",
            "release_generation",
            "release_id",
            "release_key",
            "runtime_libraries",
            "target_arch",
        },
        "compatibility manifest",
    )
    if root["format"] != FORMAT or root["target_arch"] != "aarch64":
        _fail("compatibility manifest format/target rejected")
    _validate_release_id(root["release_id"])
    _require_uint(root["release_generation"], "release_generation", 1)
    if (
        _require_uint(
            root["device_certificate_schema"],
            "device certificate schema",
            1,
            1,
        )
        != 1
    ):
        _fail("device certificate schema rejected")
    _require_hex(root["payload_manifest_sha256"], 64, "payload manifest sha256", nonzero=True)

    release_key = _require_object(root["release_key"], {"id", "public_key_sha256"}, "release key")
    _require_hex(release_key["id"], 32, "release key id", nonzero=True)
    _require_hex(release_key["public_key_sha256"], 64, "release public key sha256", nonzero=True)

    edge = _require_object(
        root["edge"],
        {"compatibility_id", "needed_guard_soname", "path", "sha256"},
        "edge compatibility",
    )
    if edge["path"] != "bin/cosmo-engine" or edge["needed_guard_soname"] != GUARD_SONAME:
        _fail("edge compatibility path/SONAME rejected")
    _require_hex(edge["sha256"], 64, "edge sha256", nonzero=True)
    _require_hex(edge["compatibility_id"], 64, "edge compatibility id", nonzero=True)

    guard = _require_object(
        root["model_guard"],
        {
            "abi_major",
            "abi_manifest_path",
            "abi_manifest_sha256",
            "dependencies_manifest_path",
            "dependencies_manifest_sha256",
            "exports",
            "exports_sha256",
            "header_path",
            "header_sha256",
            "path",
            "sha256",
            "soname",
        },
        "model guard compatibility",
    )
    if (
        _require_uint(guard["abi_major"], "guard ABI major", 2, 2) != 2
        or guard["path"] != f"lib/{GUARD_REAL_FILENAME}"
        or guard["soname"] != GUARD_SONAME
        or guard["header_path"] != GUARD_HEADER_PATH
        or guard["abi_manifest_path"] != GUARD_ABI_MANIFEST_PATH
        or guard["dependencies_manifest_path"] != GUARD_DEPENDENCY_MANIFEST_PATH
    ):
        _fail("model guard ABI bundle rejected")
    _require_hex(guard["sha256"], 64, "guard sha256", nonzero=True)
    _require_hex(guard["header_sha256"], 64, "guard header sha256", nonzero=True)
    _require_hex(guard["abi_manifest_sha256"], 64, "guard ABI manifest sha256", nonzero=True)
    _require_hex(
        guard["dependencies_manifest_sha256"],
        64,
        "guard dependency manifest sha256",
        nonzero=True,
    )
    _require_hex(guard["exports_sha256"], 64, "guard exports sha256", nonzero=True)
    if guard["exports"] != list(REQUIRED_GUARD_EXPORTS):
        _fail("model guard export whitelist rejected")
    runtime = _require_object(
        root["runtime_libraries"], {"libbmlib", "libbmrt", "libcrypto"}, "runtime libraries"
    )
    for name in ("libbmlib", "libbmrt"):
        item = _require_object(runtime[name], {"path", "sha256"}, name)
        if item["path"] != f"lib/{name}.so":
            _fail(f"{name} path rejected")
        _require_hex(item["sha256"], 64, f"{name} sha256", nonzero=True)
    libcrypto = _require_object(
        runtime["libcrypto"], {"path", "sha256", "soname", "version"}, "libcrypto"
    )
    if (
        libcrypto["path"] != "lib/libcrypto.so.3"
        or libcrypto["soname"] != "libcrypto.so.3"
        or re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", str(libcrypto["version"])) is None
    ):
        _fail("libcrypto compatibility metadata rejected")
    _require_hex(libcrypto["sha256"], 64, "libcrypto sha256", nonzero=True)
    return root


def _validate_active_compatibility_manifest(value: Any) -> Mapping[str, Any]:
    """Accept only the exact signed single-certificate schema."""
    if not isinstance(value, dict):
        _fail("active compatibility manifest must be an object")
    manifest = _validate_compatibility_manifest(value)
    if _compatibility_id(manifest) != manifest["edge"]["compatibility_id"]:
        _fail("edge/model-guard compatibility ID mismatch")
    return manifest


def _exports_digest(exports: Sequence[str]) -> str:
    return _sha256_bytes(("\n".join(exports) + "\n").encode("ascii"))


def _compatibility_id(manifest: Mapping[str, Any]) -> str:
    guard = manifest["model_guard"]
    libcrypto = manifest["runtime_libraries"]["libcrypto"]
    fields = (
        f"edge_sha256={manifest['edge']['sha256']}",
        f"guard_abi_major={guard['abi_major']}",
        f"guard_soname={guard['soname']}",
        f"guard_sha256={guard['sha256']}",
        f"guard_exports_sha256={guard['exports_sha256']}",
        f"guard_header_sha256={guard['header_sha256']}",
        f"guard_abi_manifest_sha256={guard['abi_manifest_sha256']}",
        f"guard_dependencies_manifest_sha256={guard['dependencies_manifest_sha256']}",
        f"libcrypto_version={libcrypto['version']}",
        f"libcrypto_sha256={libcrypto['sha256']}",
    )
    domain = b"cosmo-edge-guard-compatibility-v3\x00"
    return _sha256_bytes(domain + ("\n".join(fields) + "\n").encode("ascii"))


def _check_release_layout(release_root: Path) -> None:
    regular_files = {
        "bin/cosmo-engine",
        RELEASE_BOOTSTRAP_PATH,
        MODEL_PROVISION_PATH,
        f"lib/{GUARD_REAL_FILENAME}",
        "lib/libbmlib.so",
        "lib/libbmlib.so.0",
        "lib/libbmrt.so",
        "lib/libbmrt.so.1.0",
        "lib/libcrypto.so.3",
        "lib/libssl.so.3",
        GUARD_HEADER_PATH,
        GUARD_ABI_MANIFEST_PATH,
        GUARD_DEPENDENCY_MANIFEST_PATH,
        *REQUIRED_RELEASE_SCRIPTS,
    }
    for relative in regular_files:
        candidate = release_root / relative
        try:
            info = os.stat(candidate)
        except OSError as error:
            raise ReleaseError(f"required release file is unavailable: {relative}") from error
        if not stat.S_ISREG(info.st_mode):
            _fail(f"required release path is not a file: {relative}")
    for relative, target in (
        (f"lib/{GUARD_SONAME}", GUARD_REAL_FILENAME),
        ("lib/libcosmo_model_guard.so", GUARD_SONAME),
    ):
        candidate = release_root / relative
        try:
            info = os.lstat(candidate)
        except OSError as error:
            raise ReleaseError(f"required release symlink is unavailable: {relative}") from error
        if (
            not stat.S_ISLNK(info.st_mode)
            or os.readlink(candidate) != target
        ):
            _fail(f"required release symlink rejected: {relative}")
    for relative in FACADE_DIRECTORIES:
        candidate = release_root / relative
        try:
            info = os.stat(candidate)
        except OSError as error:
            raise ReleaseError(f"required release directory is unavailable: {relative}") from error
        if not stat.S_ISDIR(info.st_mode):
            _fail(f"required release directory rejected: {relative}")


def _check_component_hashes(
    release_root: Path, manifest: Mapping[str, Any]
) -> None:
    runtime_limits = {
        relative: (
            MAX_LIBCRYPTO_BYTES
            if relative == "lib/libcrypto.so.3"
            else MAX_RUNTIME_LIBRARY_BYTES
        )
        for _, _, relative in DEPENDENCY_RUNTIME_BINDINGS
    }
    libcrypto = manifest["runtime_libraries"]["libcrypto"]
    libcrypto_bytes: bytes | None = None
    runtime_digests: dict[str, str] = {}

    bindings = (
        manifest["edge"],
        manifest["model_guard"],
        manifest["runtime_libraries"]["libbmlib"],
        manifest["runtime_libraries"]["libbmrt"],
        manifest["runtime_libraries"]["libcrypto"],
    )
    for binding in bindings:
        relative = str(binding["path"])
        candidate = release_root / relative
        if relative in runtime_limits:
            digest = runtime_digests.get(relative)
            if digest is None:
                if relative == libcrypto["path"]:
                    libcrypto_bytes, digest = _read_with_digest(
                        candidate,
                        runtime_limits[relative],
                        retain_data=True,
                    )
                else:
                    digest = _sha256_limited(
                        candidate,
                        runtime_limits[relative],
                    )
                runtime_digests[relative] = digest
        else:
            digest = _sha256_file(candidate)
        if digest != binding["sha256"]:
            _fail(f"signed component digest mismatch: {relative}")
    header = release_root / manifest["model_guard"]["header_path"]
    if _sha256_file(header) != manifest["model_guard"]["header_sha256"]:
        _fail("model guard header digest mismatch")
    abi_manifest = release_root / manifest["model_guard"]["abi_manifest_path"]
    if _sha256_file(abi_manifest) != manifest["model_guard"]["abi_manifest_sha256"]:
        _fail("model guard ABI manifest digest mismatch")
    dependencies = release_root / manifest["model_guard"]["dependencies_manifest_path"]
    if (
        _sha256_file(dependencies)
        != manifest["model_guard"]["dependencies_manifest_sha256"]
    ):
        _fail("model guard dependency manifest digest mismatch")
    header_bytes = _read_exact(header, MAX_GUARD_HEADER_BYTES)
    abi_bytes = _read_exact(abi_manifest, MAX_GUARD_ABI_MANIFEST_BYTES)
    dependency_bytes = _read_exact(
        dependencies, MAX_GUARD_DEPENDENCY_MANIFEST_BYTES
    )
    _validate_model_guard_header(header_bytes)
    expected_profile = GUARD_PROFILE
    _validate_model_guard_abi_manifest(
        abi_bytes,
        expected_profile,
    )
    dependency_manifest = _validate_model_guard_dependencies(
        dependency_bytes, header_bytes, abi_bytes, expected_profile
    )
    for section, digest_key, relative in DEPENDENCY_RUNTIME_BINDINGS:
        if relative == "lib/libcrypto.so.3":
            continue
        digest = runtime_digests.get(relative)
        if digest is None:
            digest = _sha256_limited(
                release_root / relative,
                runtime_limits[relative],
            )
            runtime_digests[relative] = digest
        if digest != dependency_manifest[section][digest_key]:
            _fail(
                "Model Guard dependency runtime digest mismatch: "
                f"{relative}"
            )
    if libcrypto_bytes is None:
        _fail("internal secure libcrypto read invariant failed")
    if (
        dependency_manifest["openssl"]["version"] != libcrypto["version"]
        or dependency_manifest["openssl"]["libcrypto_soname"] != libcrypto["soname"]
        or dependency_manifest["openssl"]["libcrypto_sha256"] != libcrypto["sha256"]
    ):
        _fail("signed libcrypto metadata differs from Model Guard dependency provenance")
    if _openssl_version_from_library(libcrypto_bytes) != libcrypto["version"]:
        _fail(
            "signed libcrypto embedded OpenSSL version differs from Model Guard "
            "dependency provenance"
        )
    if _compatibility_id(manifest) != manifest["edge"]["compatibility_id"]:
        _fail("edge/model-guard compatibility ID mismatch")


class _CemV2FormatError(ValueError):
    """An unauthenticated CEM v2 core failed strict structural validation."""


@dataclasses.dataclass(frozen=True)
class _CemV2ManifestFacts:
    artifact_id: bytes
    cohort_id: bytes
    generation: int
    model_id: str
    model_version: str
    model_identity_sha256: bytes
    record_bytes: int
    source_format: str
    target_platform: str


@dataclasses.dataclass(frozen=True)
class _CemV2CoreSnapshot:
    artifact_id: str
    cohort_id: str
    core_preamble_sha256: str
    core_sha256: str
    core_size: int
    created_at: int
    generation: int
    manifest_sha256: str
    model_id: str
    model_identity_sha256: str
    model_version: str
    source_format: str
    target_platform: str


class _CemV2CborReader:
    """The bounded canonical-CBOR subset used by the Guard CEM v2 manifest."""

    def __init__(self, data: bytes) -> None:
        self._data = data
        self._offset = 0

    def _take(self, size: int) -> bytes:
        if size < 0 or size > len(self._data) - self._offset:
            raise _CemV2FormatError("truncated manifest")
        start = self._offset
        self._offset += size
        return self._data[start : start + size]

    def _read_value(self, expected_major: int) -> int:
        initial = self._take(1)[0]
        major = initial >> 5
        additional = initial & 0x1F
        if major != expected_major:
            raise _CemV2FormatError("manifest has an invalid CBOR type")
        if additional < 24:
            return additional
        widths = {24: 1, 25: 2, 26: 4, 27: 8}
        width = widths.get(additional)
        if width is None:
            raise _CemV2FormatError("manifest uses indefinite or reserved CBOR")
        value = int.from_bytes(self._take(width), "big")
        if (
            (width == 1 and value < 24)
            or (width == 2 and value <= 0xFF)
            or (width == 4 and value <= 0xFFFF)
            or (width == 8 and value <= 0xFFFFFFFF)
        ):
            raise _CemV2FormatError("manifest uses a non-minimal CBOR integer")
        return value

    def read_unsigned(self) -> int:
        return self._read_value(0)

    def read_bytes(self, expected_size: int) -> bytes:
        size = self._read_value(2)
        if size != expected_size:
            raise _CemV2FormatError("manifest byte string has an invalid size")
        return self._take(size)

    def read_text(self, maximum_size: int) -> str:
        size = self._read_value(3)
        if size == 0 or size > maximum_size:
            raise _CemV2FormatError("manifest text has an invalid size")
        try:
            return self._take(size).decode("ascii", "strict")
        except UnicodeError as error:
            raise _CemV2FormatError("manifest text is not canonical ASCII") from error

    def read_array_size(self) -> int:
        return self._read_value(4)

    def read_map_size(self) -> int:
        return self._read_value(5)

    def require_key(self, expected: int) -> None:
        if self.read_unsigned() != expected:
            raise _CemV2FormatError("manifest keys are not exactly canonical")

    def require_end(self) -> None:
        if self._offset != len(self._data):
            raise _CemV2FormatError("manifest has trailing CBOR data")


def _cem_v2_nonzero(value: bytes, description: str) -> None:
    if not any(value):
        raise _CemV2FormatError(f"{description} must be nonzero")


def _parse_canonical_cem_v2_manifest(
    manifest_bytes: bytes,
) -> _CemV2ManifestFacts:
    """Parse the public CEM v2 wire contract and return its layout bindings.

    This independently validates canonical metadata and record layout only;
    without a content key it cannot authenticate any AES-GCM record.
    """

    if not manifest_bytes or len(manifest_bytes) > CEM_V2_MAX_MANIFEST_BYTES:
        raise _CemV2FormatError("manifest has an invalid size")
    reader = _CemV2CborReader(manifest_bytes)
    if reader.read_map_size() != 15:
        raise _CemV2FormatError("manifest map must contain exactly 15 entries")

    reader.require_key(1)
    schema_version = reader.read_unsigned()
    reader.require_key(2)
    model_id = reader.read_text(64)
    reader.require_key(3)
    model_version = reader.read_text(32)
    reader.require_key(4)
    model_identity_sha256 = reader.read_bytes(32)
    reader.require_key(5)
    artifact_id = reader.read_bytes(16)
    reader.require_key(6)
    cohort_id = reader.read_bytes(16)
    reader.require_key(7)
    generation = reader.read_unsigned()
    reader.require_key(8)
    source_format = reader.read_unsigned()
    reader.require_key(9)
    target_platform = reader.read_unsigned()
    reader.require_key(10)
    segment_count = reader.read_unsigned()
    reader.require_key(11)
    chunk_count = reader.read_unsigned()
    reader.require_key(12)
    nominal_chunk_plain_len = reader.read_unsigned()
    reader.require_key(13)
    if reader.read_array_size() != 3:
        raise _CemV2FormatError("minimum Guard version must have three components")
    min_guard_version = tuple(reader.read_unsigned() for _ in range(3))

    if segment_count > CEM_V2_MAX_SEGMENTS:
        raise _CemV2FormatError("manifest has too many segments")
    reader.require_key(14)
    encoded_segment_count = reader.read_array_size()
    if encoded_segment_count != segment_count or encoded_segment_count == 0:
        raise _CemV2FormatError("manifest segment count is inconsistent")
    segments: list[tuple[int, int, int, int]] = []
    for _ in range(encoded_segment_count):
        if reader.read_array_size() != 4:
            raise _CemV2FormatError("segment descriptor has an invalid size")
        segment_index = reader.read_unsigned()
        plain_len = reader.read_unsigned()
        first_global_chunk_index = reader.read_unsigned()
        segment_chunk_count = reader.read_unsigned()
        if (
            segment_index > 0xFFFFFFFF
            or first_global_chunk_index > 0xFFFFFFFF
            or segment_chunk_count > 0xFFFFFFFF
        ):
            raise _CemV2FormatError("segment descriptor exceeds its integer width")
        segments.append(
            (
                segment_index,
                plain_len,
                first_global_chunk_index,
                segment_chunk_count,
            )
        )

    if chunk_count > CEM_V2_MAX_CHUNKS:
        raise _CemV2FormatError("manifest has too many chunks")
    reader.require_key(15)
    encoded_chunk_count = reader.read_array_size()
    if encoded_chunk_count != chunk_count or encoded_chunk_count == 0:
        raise _CemV2FormatError("manifest chunk count is inconsistent")
    chunks: list[tuple[int, int, int, int, int]] = []
    for _ in range(encoded_chunk_count):
        if reader.read_array_size() != 5:
            raise _CemV2FormatError("chunk descriptor has an invalid size")
        segment_index = reader.read_unsigned()
        global_chunk_index = reader.read_unsigned()
        plain_len = reader.read_unsigned()
        record_offset = reader.read_unsigned()
        record_len = reader.read_unsigned()
        if (
            segment_index > 0xFFFFFFFF
            or global_chunk_index > 0xFFFFFFFF
            or plain_len > 0xFFFFFFFF
            or record_len > 0xFFFFFFFF
        ):
            raise _CemV2FormatError("chunk descriptor exceeds its integer width")
        chunks.append(
            (
                segment_index,
                global_chunk_index,
                plain_len,
                record_offset,
                record_len,
            )
        )
    reader.require_end()

    if schema_version != 1 or source_format not in {1, 2} or target_platform != 1:
        raise _CemV2FormatError("manifest uses an unsupported protocol value")
    _cem_v2_nonzero(model_identity_sha256, "model identity")
    _cem_v2_nonzero(artifact_id, "artifact ID")
    _cem_v2_nonzero(cohort_id, "cohort ID")
    if generation == 0:
        raise _CemV2FormatError("generation must be nonzero")
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]{0,63}", model_id):
        raise _CemV2FormatError("model ID is not canonical")
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,31}", model_version):
        raise _CemV2FormatError("model version is not canonical")
    expected_identity = hashlib.sha256(
        MODEL_IDENTITY_DOMAIN
        + len(model_id).to_bytes(2, "big")
        + model_id.encode("ascii")
        + len(model_version).to_bytes(2, "big")
        + model_version.encode("ascii")
    ).digest()
    if model_identity_sha256 != expected_identity:
        raise _CemV2FormatError("model identity digest mismatch")
    if not (
        CEM_V2_MIN_NOMINAL_CHUNK_BYTES
        <= nominal_chunk_plain_len
        <= CEM_V2_MAX_CHUNK_PLAIN_BYTES
    ):
        raise _CemV2FormatError("nominal chunk size is outside protocol limits")
    if any(component > 0xFFFF for component in min_guard_version):
        raise _CemV2FormatError("minimum Guard version is outside protocol limits")
    if source_format == 2 and segment_count != 1:
        raise _CemV2FormatError("raw bmodel must contain exactly one segment")

    expected_record_offset = 0
    expected_global_chunk = 0
    for expected_segment_index, segment in enumerate(segments):
        (
            segment_index,
            segment_plain_len,
            first_global_chunk_index,
            segment_chunk_count,
        ) = segment
        if segment_plain_len > CEM_V2_MAX_SEGMENT_PLAIN_BYTES:
            raise _CemV2FormatError("segment plaintext size exceeds the limit")
        if (
            segment_index != expected_segment_index
            or segment_plain_len == 0
            or first_global_chunk_index != expected_global_chunk
            or segment_chunk_count == 0
            or expected_global_chunk > chunk_count
            or segment_chunk_count > chunk_count - expected_global_chunk
        ):
            raise _CemV2FormatError("segment layout is inconsistent")

        accumulated_plain_len = 0
        for local_index in range(segment_chunk_count):
            (
                chunk_segment_index,
                global_chunk_index,
                chunk_plain_len,
                record_offset,
                record_len,
            ) = chunks[expected_global_chunk]
            if (
                chunk_segment_index != expected_segment_index
                or global_chunk_index != expected_global_chunk
                or chunk_plain_len == 0
                or chunk_plain_len > nominal_chunk_plain_len
                or (
                    local_index + 1 != segment_chunk_count
                    and chunk_plain_len != nominal_chunk_plain_len
                )
                or record_offset != expected_record_offset
                or record_len != chunk_plain_len + CEM_V2_GCM_TAG_BYTES
            ):
                raise _CemV2FormatError("chunk layout is inconsistent")
            accumulated_plain_len += chunk_plain_len
            expected_record_offset += record_len
            if accumulated_plain_len > CEM_V2_MAX_SEGMENT_PLAIN_BYTES:
                raise _CemV2FormatError("segment plaintext size exceeds the limit")
            expected_global_chunk += 1
        if accumulated_plain_len != segment_plain_len:
            raise _CemV2FormatError("segment plaintext size is inconsistent")
    if expected_global_chunk != chunk_count:
        raise _CemV2FormatError("manifest leaves unassigned chunks")
    return _CemV2ManifestFacts(
        artifact_id=artifact_id,
        cohort_id=cohort_id,
        generation=generation,
        model_id=model_id,
        model_version=model_version,
        model_identity_sha256=model_identity_sha256,
        record_bytes=expected_record_offset,
        source_format=CEM_V2_SOURCE_FORMATS[source_format],
        target_platform=CEM_V2_TARGET_PLATFORM,
    )


def _pread_exact(fd: int, offset: int, size: int, limit: int) -> bytes:
    if offset < 0 or size < 0 or offset > limit or size > limit - offset:
        raise _CemV2FormatError("file range is outside the CEM v2 core")
    output = bytearray()
    while len(output) < size:
        block = os.pread(fd, size - len(output), offset + len(output))
        if not block:
            raise _CemV2FormatError("CEM v2 core is truncated")
        output.extend(block)
    return bytes(output)


def _same_file_snapshot(left: os.stat_result, right: os.stat_result) -> bool:
    return (
        left.st_dev,
        left.st_ino,
        left.st_size,
        left.st_mtime_ns,
    ) == (
        right.st_dev,
        right.st_ino,
        right.st_size,
        right.st_mtime_ns,
    )


def _validate_cem_v2_core_fd(
    fd: int, initial: os.stat_result, relative: str
) -> _CemV2CoreSnapshot:
    """Validate one CEM v2 core from one already-open, non-following fd."""

    try:
        file_size = initial.st_size
        if (
            file_size < CEM_V2_CORE_PREAMBLE_SIZE
            or file_size > CEM_V2_MAX_CORE_BYTES
        ):
            raise _CemV2FormatError("core size is outside protocol limits")
        preamble = _pread_exact(
            fd, 0, CEM_V2_CORE_PREAMBLE_SIZE, file_size
        )
        (
            magic,
            format_version,
            preamble_len,
            suite_id,
            flags,
            artifact_id,
            cohort_id,
            generation,
            created_at,
            manifest_len,
            manifest_sha256,
            nonce_prefix,
            reserved,
        ) = struct.unpack(">4sHHHH16s16sQQI32s8s8s", preamble)
        if magic != b"CEMC":
            raise _CemV2FormatError("core magic mismatch")
        if format_version != 2 or suite_id != 1 or flags != 0x0001:
            raise _CemV2FormatError("core uses an unsupported protocol value")
        if preamble_len != CEM_V2_CORE_PREAMBLE_SIZE:
            raise _CemV2FormatError("core preamble length is not canonical")
        _cem_v2_nonzero(artifact_id, "preamble artifact ID")
        _cem_v2_nonzero(cohort_id, "preamble cohort ID")
        _cem_v2_nonzero(manifest_sha256, "preamble manifest digest")
        _cem_v2_nonzero(nonce_prefix, "preamble nonce prefix")
        if generation == 0 or created_at == 0:
            raise _CemV2FormatError("preamble generation and time must be nonzero")
        if created_at > (1 << 63) - 1:
            raise _CemV2FormatError("preamble time is outside protocol limits")
        if manifest_len == 0 or manifest_len > CEM_V2_MAX_MANIFEST_BYTES:
            raise _CemV2FormatError("manifest size is outside protocol limits")
        if any(reserved):
            raise _CemV2FormatError("preamble reserved bytes must be zero")

        payload_offset = CEM_V2_CORE_PREAMBLE_SIZE + manifest_len
        if payload_offset >= file_size:
            raise _CemV2FormatError("core has no chunk-record payload")
        manifest = _pread_exact(
            fd, CEM_V2_CORE_PREAMBLE_SIZE, manifest_len, file_size
        )
        if hashlib.sha256(manifest).digest() != manifest_sha256:
            raise _CemV2FormatError("manifest digest mismatch")
        manifest_facts = _parse_canonical_cem_v2_manifest(manifest)
        if (
            manifest_facts.artifact_id != artifact_id
            or manifest_facts.cohort_id != cohort_id
            or manifest_facts.generation != generation
        ):
            raise _CemV2FormatError("preamble and manifest bindings differ")
        if payload_offset + manifest_facts.record_bytes != file_size:
            raise _CemV2FormatError("chunk-record layout does not cover the core")

        structure_after = os.fstat(fd)
        if not _same_file_snapshot(initial, structure_after):
            raise _CemV2FormatError("core changed during validation")
        core_sha256 = _sha256_fd(fd, file_size)
        after = os.fstat(fd)
        if not _same_file_snapshot(initial, after):
            raise _CemV2FormatError("core changed during validation")
        return _CemV2CoreSnapshot(
            artifact_id=artifact_id.hex(),
            cohort_id=cohort_id.hex(),
            core_preamble_sha256=_sha256_bytes(preamble),
            core_sha256=core_sha256,
            core_size=file_size,
            created_at=created_at,
            generation=generation,
            manifest_sha256=manifest_sha256.hex(),
            model_id=manifest_facts.model_id,
            model_identity_sha256=manifest_facts.model_identity_sha256.hex(),
            model_version=manifest_facts.model_version,
            source_format=manifest_facts.source_format,
            target_platform=manifest_facts.target_platform,
        )
    except (OSError, struct.error, _CemV2FormatError) as error:
        raise ReleaseError(
            f"invalid CEM v2 preset blocks upgrade: {relative}: {error}"
        ) from error


def _sha256_fd(fd: int, file_size: int) -> str:
    digest = hashlib.sha256()
    offset = 0
    while offset < file_size:
        block = os.pread(fd, min(1024 * 1024, file_size - offset), offset)
        if not block:
            _fail("preset model changed or became unreadable during validation")
        digest.update(block)
        offset += len(block)
    return digest.hexdigest()


def _scan_preset_models(
    release_root: Path,
) -> tuple[_CemV2CoreSnapshot, ...]:
    """Validate every preset and require one nonzero package-wide cohort ID."""
    resource = release_root / "resource"
    models = resource / "models"
    try:
        models_info = os.lstat(models)
    except FileNotFoundError:
        return ()
    if not stat.S_ISDIR(models_info.st_mode):
        _fail("preset model root rejected")
    snapshots: list[_CemV2CoreSnapshot] = []
    for current_root, directories, files in os.walk(models, followlinks=False):
        directories.sort()
        files.sort()
        for directory in directories:
            info = os.lstat(Path(current_root) / directory)
            if not stat.S_ISDIR(info.st_mode):
                _fail("preset model directory replacement rejected")
            if directory == "model.nn":
                _fail("preset model path must be a regular file")
        for filename in files:
            if filename != "model.nn":
                continue
            candidate = Path(current_root) / filename
            relative = candidate.relative_to(models).as_posix()
            try:
                fd = os.open(
                    candidate,
                    os.O_RDONLY
                    | os.O_CLOEXEC
                    | getattr(os, "O_NONBLOCK", 0),
                )
            except OSError as error:
                raise ReleaseError(f"preset model open rejected: {relative}") from error
            try:
                info = os.fstat(fd)
                if not stat.S_ISREG(info.st_mode):
                    _fail("preset model must be a regular file")
                header = os.pread(fd, 8, 0)
                if header[:4] != b"CEMC":
                    _fail(
                        f"plaintext or unknown preset model blocks upgrade: {relative}"
                    )
                snapshots.append(
                    _validate_cem_v2_core_fd(fd, info, relative)
                )
            except OSError as error:
                raise ReleaseError(
                    f"preset model validation failed: {relative}"
                ) from error
            finally:
                os.close(fd)
    if snapshots:
        cohort_ids = {snapshot.cohort_id for snapshot in snapshots}
        if "0" * 32 in cohort_ids:
            _fail("preset model cohort ID must be nonzero")
        if len(cohort_ids) != 1:
            _fail("preset models use mixed cohort IDs")
    return tuple(snapshots)


def _state_tree_fingerprint(root: Path) -> str:
    digest = hashlib.sha256(b"cosmo-model-guard-persistent-state-v2\x00")
    try:
        os.lstat(root)
    except FileNotFoundError:
        digest.update(b"absent")
        return digest.hexdigest()

    certificate = root / "device-certificate.bin"
    try:
        initial = os.stat(certificate)
    except FileNotFoundError:
        return digest.hexdigest()
    except OSError as error:
        raise ReleaseError("model-guard device certificate is unavailable") from error
    if not stat.S_ISREG(initial.st_mode):
        _fail("model-guard device certificate must resolve to a regular file")

    try:
        fd = os.open(certificate, os.O_RDONLY | os.O_CLOEXEC)
    except OSError as error:
        raise ReleaseError("model-guard device certificate cannot be opened") from error
    try:
        opened = os.fstat(fd)
        if not _same_file_snapshot(initial, opened):
            _fail("model-guard device certificate changed while opening")
        certificate_digest = hashlib.sha256()
        while True:
            block = os.read(fd, 1024 * 1024)
            if not block:
                break
            certificate_digest.update(block)
        current = os.fstat(fd)
        path_current = os.stat(certificate)
        if (
            not _same_file_snapshot(initial, current)
            or not _same_file_snapshot(initial, path_current)
        ):
            _fail("model-guard device certificate changed while reading")
    except OSError as error:
        raise ReleaseError("model-guard device certificate read failed") from error
    finally:
        os.close(fd)

    relative = b"device-certificate.bin"
    digest.update(b"F")
    digest.update(len(relative).to_bytes(4, "big"))
    digest.update(relative)
    digest.update(initial.st_size.to_bytes(8, "big"))
    digest.update(certificate_digest.digest())
    return digest.hexdigest()


@dataclasses.dataclass(frozen=True)
class ArchiveInspection:
    manifest_bytes: bytes
    manifest: Mapping[str, Any]
    signature: bytes
    payload_bytes: bytes
    payload_entries: tuple[Mapping[str, Any], ...]
    members: tuple[tarfile.TarInfo, ...]


@dataclasses.dataclass(frozen=True)
class BootstrapHealthPlan:
    run_script: Path
    health_script: Path
    stop_script: Path
    log_path: Path


class ReleaseUpdater:
    def __init__(
        self,
        paths: ReleasePaths,
        *,
        failpoint: str | None = None,
        failure_callback: Callable[[str], None] | None = None,
        lifecycle_callback: Callable[[str], None] | None = None,
    ) -> None:
        self.paths = paths
        self.failpoint = failpoint
        self.failure_callback = failure_callback
        self.lifecycle_callback = lifecycle_callback

    def _interrupt(self, point: str) -> None:
        if self.failure_callback is not None:
            self.failure_callback(point)
        if self.failpoint == point:
            raise InjectedInterruption(point)

    def _lifecycle(self, event: str) -> None:
        if self.lifecycle_callback is not None:
            self.lifecycle_callback(event)

    def _initialize_directories(self) -> None:
        self.paths.install_root.mkdir(mode=0o755, parents=True, exist_ok=True)
        _directory(self.paths.install_root)
        self.paths.state_dir.mkdir(mode=0o700, exist_ok=True)
        self.paths.releases.mkdir(mode=0o700, exist_ok=True)
        self.paths.transactions.mkdir(mode=0o700, exist_ok=True)
        _directory(self.paths.state_dir)
        _directory(self.paths.releases)
        _directory(self.paths.transactions)

    @contextlib.contextmanager
    def _lock(self) -> Iterator[None]:
        self._initialize_directories()
        fd = os.open(
            self.paths.lock_file,
            os.O_RDWR | os.O_CREAT | os.O_CLOEXEC | os.O_NOFOLLOW,
            0o600,
        )
        try:
            info = os.fstat(fd)
            if not stat.S_ISREG(info.st_mode):
                _fail("release update lock rejected")
            fcntl.flock(fd, fcntl.LOCK_EX)
            yield
        finally:
            os.close(fd)

    def _load_state(self) -> Mapping[str, Any]:
        data = _read_exact(self.paths.state_file, MAX_MANIFEST_BYTES)
        value = _strict_json(data, MAX_MANIFEST_BYTES, "release compatibility state")
        state = _require_object(
            value,
            {
                "current_release_id",
                "format",
                "manifest_sha256",
                "release_generation",
                "release_key_id",
                "release_public_key_sha256",
            },
            "release compatibility state",
        )
        if state["format"] != STATE_FORMAT:
            _fail("release state format rejected")
        _validate_release_id(state["current_release_id"])
        _require_uint(state["release_generation"], "state release generation", 1)
        _require_hex(state["manifest_sha256"], 64, "state manifest sha256", nonzero=True)
        _require_hex(state["release_key_id"], 32, "state release key id", nonzero=True)
        _require_hex(state["release_public_key_sha256"], 64, "state public key sha256", nonzero=True)
        return state

    def _current_release_path(self, state: Mapping[str, Any]) -> Path:
        info = os.lstat(self.paths.current)
        if not stat.S_ISLNK(info.st_mode):
            _fail("active release pointer rejected")
        target = os.readlink(self.paths.current)
        expected = f".releases/{state['current_release_id']}"
        if target != expected:
            _fail("active release pointer does not match compatibility state")
        release = self.paths.releases / state["current_release_id"]
        _directory(release)
        return release

    def _validate_facades(self, release: Path) -> None:
        for name in FACADE_DIRECTORIES:
            facade = self.paths.install_root / name
            info = os.lstat(facade)
            if (
                not stat.S_ISLNK(info.st_mode)
                or os.readlink(facade) != f"current/{name}"
            ):
                _fail(f"release facade rejected: {name}")
            target = release / name
            target_info = os.stat(target)
            if not stat.S_ISDIR(target_info.st_mode):
                _fail(f"active release facade target rejected: {name}")

    def _validate_current_trust(self) -> tuple[Mapping[str, Any], Path, Mapping[str, Any]]:
        state = self._load_state()
        release = self._current_release_path(state)
        self._validate_facades(release)
        manifest = self._validate_release_trust_against_state(state, release)
        return state, release, manifest

    def _validate_release_trust_against_state(
        self, state: Mapping[str, Any], release: Path
    ) -> Mapping[str, Any]:
        if (
            release.parent != self.paths.releases
            or release.name != state["current_release_id"]
        ):
            _fail("trusted release path does not match durable state")
        _directory(release)
        meta = release / "meta"
        _directory(meta)
        public_key = meta / "release-public-key.pem"
        key_bytes = _read_exact(public_key, 16 * 1024)
        if _sha256_bytes(key_bytes) != state["release_public_key_sha256"]:
            _fail("active release trust anchor digest mismatch")
        manifest_path = meta / "compatibility.manifest.json"
        signature_path = meta / "compatibility.manifest.sig"
        manifest_bytes = _read_exact(manifest_path, MAX_MANIFEST_BYTES)
        signature = _read_exact(signature_path, 64)
        if len(signature) != 64:
            _fail("active release signature size rejected")
        _verify_ed25519(self.paths.openssl, public_key, manifest_path, signature_path)
        manifest = _validate_active_compatibility_manifest(
            _strict_json(manifest_bytes, MAX_MANIFEST_BYTES, "active compatibility manifest")
        )
        if (
            _sha256_bytes(manifest_bytes) != state["manifest_sha256"]
            or manifest["release_id"] != state["current_release_id"]
            or manifest["release_generation"] != state["release_generation"]
            or manifest["release_key"]["id"] != state["release_key_id"]
            or manifest["release_key"]["public_key_sha256"] != state["release_public_key_sha256"]
        ):
            _fail("active signed compatibility set does not match durable state")
        return manifest

    def _load_journal(self) -> Mapping[str, Any] | None:
        if not self.paths.journal_file.exists():
            return None
        data = _read_exact(self.paths.journal_file, MAX_MANIFEST_BYTES)
        value = _strict_json(data, MAX_MANIFEST_BYTES, "release transaction journal")
        journal = _require_object(
            value,
            {
                "archive_sha256",
                "format",
                "incoming_manifest_sha256",
                "incoming_release_id",
                "persistent_state_fingerprint",
                "phase",
                "previous_release_id",
                "publication_marker_sha256",
                "transaction_id",
            },
            "release transaction journal",
        )
        if journal["format"] != JOURNAL_FORMAT:
            _fail("release journal format rejected")
        _require_string(journal["transaction_id"], "transaction ID", r"[0-9a-f]{32}")
        _validate_release_id(journal["previous_release_id"])
        _validate_release_id(journal["incoming_release_id"])
        _require_hex(journal["archive_sha256"], 64, "journal archive sha256", nonzero=True)
        _require_hex(journal["incoming_manifest_sha256"], 64, "journal manifest sha256", nonzero=True)
        _require_hex(journal["persistent_state_fingerprint"], 64, "journal persistent-state fingerprint")
        _require_hex(
            journal["publication_marker_sha256"],
            64,
            "journal publication marker sha256",
            nonzero=True,
        )
        if journal["phase"] not in ("preparing", "publishing", "staged", "switched"):
            _fail("release journal phase rejected")
        return journal

    def _write_journal(self, journal: Mapping[str, Any]) -> None:
        _atomic_write(
            self.paths.journal_file,
            _canonical_json(dict(journal)),
            0o600,
        )

    def _validate_transaction_owned_release(
        self, journal: Mapping[str, Any], release: Path
    ) -> None:
        """Prove that a published tree was created by this exact transaction."""
        if (
            release.parent != self.paths.releases
            or release.name != journal["incoming_release_id"]
        ):
            _fail("published release path does not match the transaction")
        _directory(release)
        meta = release / "meta"
        _directory(meta)

        marker = _read_exact(
            release / PUBLICATION_MARKER_PATH,
            MAX_MANIFEST_BYTES,
        )
        expected_marker = _publication_marker_bytes(
            str(journal["transaction_id"]),
            str(journal["incoming_release_id"]),
            str(journal["incoming_manifest_sha256"]),
            str(journal["archive_sha256"]),
        )
        if (
            marker != expected_marker
            or _sha256_bytes(marker) != journal["publication_marker_sha256"]
        ):
            _fail("published release is not owned by the pending transaction")

        manifest_bytes = _read_exact(
            meta / "compatibility.manifest.json",
            MAX_MANIFEST_BYTES,
        )
        if _sha256_bytes(manifest_bytes) != journal["incoming_manifest_sha256"]:
            _fail("transaction-owned release manifest changed")

    def _transaction_release_for_cleanup(
        self, journal: Mapping[str, Any]
    ) -> Path | None:
        """Resolve only a tree whose publication is proven by this journal."""
        release = self.paths.releases / journal["incoming_release_id"]
        present = release.exists() or release.is_symlink()
        phase = journal["phase"]
        if phase == "preparing":
            if present:
                _fail("preparing transaction encountered a non-transaction release tree")
            return None
        if not present:
            if phase == "publishing":
                return None
            _fail("published incoming release tree is missing")
        self._validate_transaction_owned_release(journal, release)
        return release

    def _switch_current(self, release_id: str) -> None:
        _validate_release_id(release_id)
        destination = self.paths.releases / release_id
        _directory(destination)
        temporary = self.paths.install_root / f".current-{uuid.uuid4().hex}"
        os.symlink(f".releases/{release_id}", temporary)
        try:
            info = os.lstat(temporary)
            if not stat.S_ISLNK(info.st_mode):
                _fail("temporary active release pointer rejected")
            os.replace(temporary, self.paths.current)
            _fsync_directory(self.paths.install_root)
        except BaseException:
            with contextlib.suppress(OSError):
                os.unlink(temporary)
            raise

    def _copy_input_archive(self, archive: Path, destination: Path) -> str:
        """Copy one validated input inode into the private transaction tree.

        The caller-supplied path is opened exactly once.  All later parsing and
        extraction operates on the returned private inode, so replacing or
        modifying the caller's path cannot change the authenticated input.
        """
        try:
            path_info = os.stat(archive)
        except OSError as error:
            raise ReleaseError("release archive is unavailable") from error
        if (
            not stat.S_ISREG(path_info.st_mode)
            or path_info.st_size <= 0
            or path_info.st_size > MAX_ARCHIVE_BYTES
        ):
            _fail("release archive type or size rejected")

        try:
            source_fd = os.open(archive, os.O_RDONLY | os.O_CLOEXEC)
        except OSError as error:
            raise ReleaseError("release archive cannot be opened securely") from error
        output_fd = -1
        try:
            opened_info = os.fstat(source_fd)
            if not _same_file_snapshot(path_info, opened_info):
                _fail("release archive changed while opening")

            output_fd = os.open(
                destination,
                os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC | os.O_NOFOLLOW,
                0o600,
            )
            digest = hashlib.sha256()
            copied = 0
            while True:
                block = os.read(source_fd, 1024 * 1024)
                if not block:
                    break
                copied += len(block)
                if copied > opened_info.st_size:
                    _fail("release archive grew while copying")
                digest.update(block)
                offset = 0
                while offset < len(block):
                    offset += os.write(output_fd, block[offset:])
            if copied != opened_info.st_size:
                _fail("release archive size changed while copying")
            os.fdatasync(output_fd)
            os.fchmod(output_fd, 0o600)
            private_info = os.fstat(output_fd)
            if (
                not stat.S_ISREG(private_info.st_mode)
                or private_info.st_size != copied
            ):
                _fail("private release archive inode rejected")

            current_info = os.fstat(source_fd)
            if not _same_file_snapshot(path_info, current_info):
                _fail("release archive inode changed while copying")
            _fsync_directory(destination.parent)
            return digest.hexdigest()
        except BaseException:
            with contextlib.suppress(OSError):
                os.unlink(destination)
            raise
        finally:
            if output_fd >= 0:
                os.close(output_fd)
            os.close(source_fd)

    def _archive_members(self, archive: Path) -> tuple[tuple[tarfile.TarInfo, ...], dict[str, bytes]]:
        info = os.lstat(archive)
        if (
            not stat.S_ISREG(info.st_mode)
            or info.st_size <= 0
            or info.st_size > MAX_ARCHIVE_BYTES
        ):
            _fail("release archive type or size rejected")
        fd = os.open(archive, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW)
        metadata: dict[str, bytes] = {}
        members: list[tarfile.TarInfo] = []
        names: set[str] = set()
        total = 0
        try:
            with os.fdopen(fd, "rb", closefd=False) as stream, tarfile.open(fileobj=stream, mode="r:gz") as bundle:
                for member in bundle:
                    if len(members) >= MAX_ARCHIVE_ENTRIES:
                        _fail("release archive has too many entries")
                    name = _canonical_relative_path(member.name, "archive member path")
                    if not (
                        name in ("payload", "meta")
                        or name.startswith("payload/")
                        or name.startswith("meta/")
                    ):
                        _fail("release archive contains an unexpected top-level path")
                    if name in names:
                        _fail("release archive contains duplicate member names")
                    names.add(name)
                    if member.islnk() or member.ischr() or member.isblk() or member.isfifo() or member.isdev():
                        _fail("release archive contains a forbidden object")
                    if not (member.isfile() or member.isdir() or member.issym()):
                        _fail("release archive member type rejected")
                    if member.sparse is not None:
                        _fail("sparse release archive members are forbidden")
                    total += member.size
                    if total > MAX_ARCHIVE_BYTES:
                        _fail("release archive expanded size exceeds the limit")
                    if member.issym():
                        _safe_symlink_target(name, member.linkname)
                    if name.startswith("meta/"):
                        if name not in {
                            "meta/compatibility.manifest.json",
                            "meta/compatibility.manifest.sig",
                            "meta/payload.files.json",
                        } or not member.isfile():
                            _fail("release archive metadata set rejected")
                        maximum = 64 if name.endswith(".sig") else (
                            MAX_PAYLOAD_MANIFEST_BYTES if name.endswith("payload.files.json") else MAX_MANIFEST_BYTES
                        )
                        if member.size > maximum:
                            _fail("release archive metadata is too large")
                        extracted = bundle.extractfile(member)
                        if extracted is None:
                            _fail("cannot read release archive metadata")
                        data = extracted.read(maximum + 1)
                        if len(data) != member.size or len(data) > maximum:
                            _fail("release archive metadata length mismatch")
                        metadata[name] = data
                    members.append(member)
        except (tarfile.TarError, OSError) as error:
            if isinstance(error, ReleaseError):
                raise
            raise ReleaseError("release archive parsing failed") from error
        finally:
            os.close(fd)
        if set(metadata) != {
            "meta/compatibility.manifest.json",
            "meta/compatibility.manifest.sig",
            "meta/payload.files.json",
        }:
            _fail("release archive metadata is incomplete")
        return tuple(members), metadata

    def _inspect_archive(
        self,
        archive: Path,
        state: Mapping[str, Any],
        current_release: Path,
        current_manifest: Mapping[str, Any],
    ) -> ArchiveInspection:
        members, metadata = self._archive_members(archive)
        manifest_bytes = metadata["meta/compatibility.manifest.json"]
        signature = metadata["meta/compatibility.manifest.sig"]
        payload_bytes = metadata["meta/payload.files.json"]
        if len(signature) != 64:
            _fail("incoming release signature size rejected")
        manifest = _validate_compatibility_manifest(
            _strict_json(manifest_bytes, MAX_MANIFEST_BYTES, "incoming compatibility manifest")
        )
        payload_entries = _validate_payload_manifest(
            _strict_json(payload_bytes, MAX_PAYLOAD_MANIFEST_BYTES, "incoming payload manifest")
        )
        if _sha256_bytes(payload_bytes) != manifest["payload_manifest_sha256"]:
            _fail("incoming payload manifest digest mismatch")
        if (
            manifest["release_key"]["id"] != state["release_key_id"]
            or manifest["release_key"]["public_key_sha256"] != state["release_public_key_sha256"]
        ):
            _fail("incoming package attempted to select a different release trust anchor")
        if manifest["release_generation"] <= state["release_generation"]:
            _fail("incoming release is not a strict generation upgrade")
        if manifest["release_id"] == current_manifest["release_id"]:
            _fail("incoming release ID is already active")

        with tempfile.TemporaryDirectory(prefix="cosmo-release-signature-", dir=self.paths.state_dir) as temporary:
            temporary_path = Path(temporary)
            message_path = temporary_path / "manifest"
            signature_path = temporary_path / "signature"
            message_path.write_bytes(manifest_bytes)
            signature_path.write_bytes(signature)
            os.chmod(message_path, 0o600)
            os.chmod(signature_path, 0o600)
            _verify_ed25519(
                self.paths.openssl,
                current_release / "meta/release-public-key.pem",
                message_path,
                signature_path,
            )

        expected: dict[str, tuple[str, Mapping[str, Any] | None]] = {
            "meta": ("directory", None),
            "meta/compatibility.manifest.json": ("file", None),
            "meta/compatibility.manifest.sig": ("file", None),
            "meta/payload.files.json": ("file", None),
            "payload": ("directory", None),
        }
        for entry in payload_entries:
            expected[f"payload/{entry['path']}"] = (str(entry["type"]), entry)
        actual = {member.name: member for member in members}
        if set(actual) != set(expected):
            _fail("release archive does not exactly match its payload manifest")
        for name, (expected_type, entry) in expected.items():
            member = actual[name]
            if name in ("meta", "payload"):
                if not member.isdir():
                    _fail("release archive top-level directory type rejected")
                continue
            if name.startswith("meta/"):
                continue
            assert entry is not None
            actual_type = "file" if member.isfile() else "directory" if member.isdir() else "symlink"
            if actual_type != expected_type:
                _fail(f"archive type differs from payload manifest: {name}")
            if expected_type == "file" and member.size != entry["size"]:
                _fail(f"archive file size differs from payload manifest: {name}")
            if expected_type == "symlink" and member.linkname != entry["target"]:
                _fail(f"archive symlink differs from payload manifest: {name}")
        return ArchiveInspection(
            manifest_bytes=manifest_bytes,
            manifest=manifest,
            signature=signature,
            payload_bytes=payload_bytes,
            payload_entries=payload_entries,
            members=members,
        )

    def _extract_archive(self, archive: Path, inspection: ArchiveInspection, destination: Path) -> Path:
        destination.mkdir(mode=0o700)
        extracted_root = destination / "extracted"
        extracted_root.mkdir(mode=0o700)
        entries = {f"payload/{entry['path']}": entry for entry in inspection.payload_entries}
        expected: dict[str, tuple[str, Mapping[str, Any] | None]] = {
            "meta": ("directory", None),
            "meta/compatibility.manifest.json": ("file", None),
            "meta/compatibility.manifest.sig": ("file", None),
            "meta/payload.files.json": ("file", None),
            "payload": ("directory", None),
        }
        for entry in inspection.payload_entries:
            expected[f"payload/{entry['path']}"] = (str(entry["type"]), entry)
        metadata_digests = {
            "meta/compatibility.manifest.json": _sha256_bytes(inspection.manifest_bytes),
            "meta/compatibility.manifest.sig": _sha256_bytes(inspection.signature),
            "meta/payload.files.json": _sha256_bytes(inspection.payload_bytes),
        }
        fd = os.open(archive, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW)
        try:
            with os.fdopen(fd, "rb", closefd=False) as stream, tarfile.open(fileobj=stream, mode="r:gz") as bundle:
                members: dict[str, tarfile.TarInfo] = {}
                expanded_size = 0
                for member in bundle:
                    if len(members) >= MAX_ARCHIVE_ENTRIES:
                        _fail("release archive has too many entries during extraction")
                    name = _canonical_relative_path(member.name, "archive member path during extraction")
                    if name in members:
                        _fail("release archive contains duplicate members during extraction")
                    if not (
                        name in ("payload", "meta")
                        or name.startswith("payload/")
                        or name.startswith("meta/")
                    ):
                        _fail("release archive path set changed during extraction")
                    if member.islnk() or member.ischr() or member.isblk() or member.isfifo() or member.isdev():
                        _fail("release archive contains a forbidden object during extraction")
                    if not (member.isfile() or member.isdir() or member.issym()):
                        _fail("release archive member type changed during extraction")
                    if member.sparse is not None:
                        _fail("sparse release archive member appeared during extraction")
                    expanded_size += member.size
                    if expanded_size > MAX_ARCHIVE_BYTES:
                        _fail("release archive expanded size exceeds the limit during extraction")
                    if member.issym():
                        _safe_symlink_target(name, member.linkname)
                    members[name] = member

                if set(members) != set(expected):
                    _fail("release archive member set changed during extraction")
                for name, (expected_type, entry) in expected.items():
                    member = members[name]
                    actual_type = (
                        "file" if member.isfile() else "directory" if member.isdir() else "symlink"
                    )
                    if actual_type != expected_type:
                        _fail(f"release archive member type changed during extraction: {name}")
                    if name not in ("meta", "payload") and not name.startswith("meta/"):
                        assert entry is not None
                        if expected_type == "file" and member.size != entry["size"]:
                            _fail(f"archive file size changed during extraction: {name}")
                        if expected_type == "symlink" and member.linkname != entry["target"]:
                            _fail(f"archive symlink changed during extraction: {name}")

                for member in sorted(members.values(), key=lambda item: (item.name.count("/"), item.name.encode("utf-8"))):
                    target = extracted_root / member.name
                    parent = target.parent
                    parent.mkdir(mode=0o700, parents=True, exist_ok=True)
                    if member.isdir():
                        target.mkdir(mode=0o700, exist_ok=False)
                        continue
                    if member.issym():
                        continue
                    source = bundle.extractfile(member)
                    if source is None:
                        _fail("cannot extract signed release file")
                    output_fd = os.open(
                        target,
                        os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC | os.O_NOFOLLOW,
                        0o600,
                    )
                    digest = hashlib.sha256()
                    count = 0
                    try:
                        while True:
                            block = source.read(1024 * 1024)
                            if not block:
                                break
                            count += len(block)
                            if count > member.size:
                                _fail("archive file grew while extracting")
                            digest.update(block)
                            offset = 0
                            while offset < len(block):
                                offset += os.write(output_fd, block[offset:])
                        if count != member.size:
                            _fail("archive file length mismatch")
                        os.fdatasync(output_fd)
                    finally:
                        os.close(output_fd)
                    entry = entries.get(member.name)
                    if entry is not None:
                        if digest.hexdigest() != entry["sha256"]:
                            _fail(f"payload file digest mismatch: {entry['path']}")
                        os.chmod(target, entry["mode"], follow_symlinks=False)
                    else:
                        if digest.hexdigest() != metadata_digests[member.name]:
                            _fail(f"release archive metadata changed during extraction: {member.name}")
                        os.chmod(target, 0o600, follow_symlinks=False)

                for entry in inspection.payload_entries:
                    if entry["type"] != "symlink":
                        continue
                    target = extracted_root / "payload" / entry["path"]
                    os.symlink(entry["target"], target)

            payload_root = extracted_root / "payload"
            meta_root = extracted_root / "meta"
            for entry in inspection.payload_entries:
                candidate = payload_root / entry["path"]
                info = os.lstat(candidate)
                if entry["type"] == "file" and (
                    not stat.S_ISREG(info.st_mode)
                    or info.st_size != entry["size"]
                ):
                    _fail(f"extracted file validation failed: {entry['path']}")
                if entry["type"] == "directory" and not stat.S_ISDIR(info.st_mode):
                    _fail(f"extracted directory validation failed: {entry['path']}")
                if entry["type"] == "symlink" and (
                    not stat.S_ISLNK(info.st_mode) or os.readlink(candidate) != entry["target"]
                ):
                    _fail(f"extracted symlink validation failed: {entry['path']}")
            for entry in sorted(
                (item for item in inspection.payload_entries if item["type"] == "directory"),
                key=lambda item: str(item["path"]).count("/"),
                reverse=True,
            ):
                os.chmod(payload_root / entry["path"], entry["mode"])
            os.chmod(payload_root, 0o755)
            os.chmod(meta_root, 0o700)
            _fsync_directory(meta_root)
            _fsync_directory(payload_root)
            _fsync_directory(extracted_root)
            return extracted_root
        finally:
            os.close(fd)

    def _cleanup_unjournaled_transactions(self) -> None:
        """Remove only recognizable private transactions when no journal owns them."""
        for candidate in sorted(self.paths.transactions.iterdir(), key=lambda item: item.name):
            if re.fullmatch(r"[0-9a-f]{32}", candidate.name) is None:
                _fail("unexpected unjournaled object in the transaction directory")
            info = os.lstat(candidate)
            if not stat.S_ISDIR(info.st_mode):
                _fail("unjournaled transaction path is not a directory")
            _remove_private_tree(candidate, self.paths.transactions)

    def prepare(self, archive: Path) -> Mapping[str, Any]:
        archive = Path(os.path.abspath(os.fspath(archive)))
        with self._lock():
            if self._load_journal() is not None:
                _fail("another release transaction is pending; recover it first")
            if self._load_bootstrap_journal() is not None:
                _fail("a release bootstrap transaction is pending; recover it first")
            self._cleanup_unjournaled_transactions()
            state, current_release, current_manifest = self._validate_current_trust()
            transaction_id = uuid.uuid4().hex
            transaction_root = self.paths.transactions / transaction_id
            transaction_root.mkdir(mode=0o700)
            controlled_archive = transaction_root / "signed-release.tar.gz"
            try:
                archive_sha256 = self._copy_input_archive(archive, controlled_archive)
                inspection = self._inspect_archive(
                    controlled_archive, state, current_release, current_manifest
                )
                release_id = inspection.manifest["release_id"]
                staged = self.paths.releases / release_id
                if staged.exists() or staged.is_symlink():
                    _fail("incoming release ID already exists")
                persistent_fingerprint = _state_tree_fingerprint(
                    self.paths.model_guard_state_root
                )
                incoming_manifest_sha256 = _sha256_bytes(inspection.manifest_bytes)
                publication_marker = _publication_marker_bytes(
                    transaction_id,
                    release_id,
                    incoming_manifest_sha256,
                    archive_sha256,
                )
                journal: dict[str, Any] = {
                    "archive_sha256": archive_sha256,
                    "format": JOURNAL_FORMAT,
                    "incoming_manifest_sha256": incoming_manifest_sha256,
                    "incoming_release_id": release_id,
                    "persistent_state_fingerprint": persistent_fingerprint,
                    "phase": "preparing",
                    "previous_release_id": state["current_release_id"],
                    "publication_marker_sha256": _sha256_bytes(publication_marker),
                    "transaction_id": transaction_id,
                }
                self._write_journal(journal)
                self._interrupt("after_journal")
                extracted_root = self._extract_archive(
                    controlled_archive, inspection, transaction_root / "work"
                )
                self._interrupt("after_extract")

                payload_root = extracted_root / "payload"
                meta_root = extracted_root / "meta"
                os.rename(meta_root, payload_root / "meta")
                trusted_key = current_release / "meta/release-public-key.pem"
                key_bytes = _read_exact(trusted_key, 16 * 1024)
                key_path = payload_root / "meta/release-public-key.pem"
                key_fd = os.open(
                    key_path,
                    os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC | os.O_NOFOLLOW,
                    0o600,
                )
                try:
                    os.write(key_fd, key_bytes)
                    os.fdatasync(key_fd)
                finally:
                    os.close(key_fd)
                _fsync_directory(payload_root / "meta")

                _check_release_layout(payload_root)
                _check_component_hashes(payload_root, inspection.manifest)
                _scan_preset_models(payload_root)
                if (
                    _state_tree_fingerprint(self.paths.model_guard_state_root)
                    != persistent_fingerprint
                ):
                    _fail("model-guard state changed during release preparation")

                _atomic_write(
                    payload_root / PUBLICATION_MARKER_PATH,
                    publication_marker,
                    0o600,
                )
                self._interrupt("after_publication_marker")
                journal["phase"] = "publishing"
                self._write_journal(journal)
                self._interrupt("before_release_publish")
                _rename_noreplace(payload_root, staged)
                _fsync_directory(self.paths.releases)
                self._interrupt("after_release_rename")
                journal["phase"] = "staged"
                self._write_journal(journal)
                self._interrupt("after_release_publish")
                _remove_private_tree(transaction_root, self.paths.transactions)
                return inspection.manifest
            except BaseException:
                # Before a journal exists, nobody else can discover this private
                # tree for recovery.  Remove that exact orphan immediately.
                if not self.paths.journal_file.exists() and transaction_root.exists():
                    _remove_private_tree(transaction_root, self.paths.transactions)
                raise

    def activate(self) -> Path:
        with self._lock():
            journal = self._load_journal()
            if journal is None or journal["phase"] != "staged":
                _fail("no staged release is ready for activation")
            state, _, _ = self._validate_current_trust()
            if state["current_release_id"] != journal["previous_release_id"]:
                _fail("active release changed after staging")
            if (
                _state_tree_fingerprint(self.paths.model_guard_state_root)
                != journal["persistent_state_fingerprint"]
            ):
                _fail("model-guard state changed before activation")
            staged = self._transaction_release_for_cleanup(journal)
            if staged is None:
                _fail("staged release publication is incomplete")
            self._switch_current(journal["incoming_release_id"])
            journal = dict(journal)
            journal["phase"] = "switched"
            self._write_journal(journal)
            self._interrupt("after_switch")
            return staged

    def commit_healthy(self) -> Mapping[str, Any]:
        with self._lock():
            journal = self._load_journal()
            if journal is None or journal["phase"] != "switched":
                _fail("no activated release is awaiting health acceptance")
            previous_state = self._load_state()
            if previous_state["current_release_id"] != journal["previous_release_id"]:
                _fail("durable release state changed during health validation")
            current_info = os.lstat(self.paths.current)
            if not stat.S_ISLNK(current_info.st_mode) or os.readlink(self.paths.current) != f".releases/{journal['incoming_release_id']}":
                _fail("active pointer changed during health validation")
            release = self.paths.releases / journal["incoming_release_id"]
            self._validate_transaction_owned_release(journal, release)
            meta = release / "meta"
            manifest_bytes = _read_exact(
                meta / "compatibility.manifest.json",
                MAX_MANIFEST_BYTES,
            )
            if _sha256_bytes(manifest_bytes) != journal["incoming_manifest_sha256"]:
                _fail("activated release manifest changed before commit")
            manifest = _validate_compatibility_manifest(
                _strict_json(manifest_bytes, MAX_MANIFEST_BYTES, "activated compatibility manifest")
            )
            if (
                _state_tree_fingerprint(self.paths.model_guard_state_root)
                != journal["persistent_state_fingerprint"]
            ):
                _fail("model-guard state changed during startup validation")
            state = {
                "current_release_id": manifest["release_id"],
                "format": STATE_FORMAT,
                "manifest_sha256": _sha256_bytes(manifest_bytes),
                "release_generation": manifest["release_generation"],
                "release_key_id": manifest["release_key"]["id"],
                "release_public_key_sha256": manifest["release_key"]["public_key_sha256"],
            }
            _atomic_write(
                self.paths.state_file,
                _canonical_json(state),
                0o600,
            )
            self._interrupt("after_state_commit")
            os.unlink(self.paths.journal_file)
            _fsync_directory(self.paths.state_dir)
            return manifest

    def _complete_committed_journal(
        self, journal: Mapping[str, Any], state: Mapping[str, Any]
    ) -> Path | None:
        """Finish journal cleanup when the durable incoming state already won."""
        if state["current_release_id"] != journal["incoming_release_id"]:
            return None
        if (
            journal["phase"] != "switched"
            or state["manifest_sha256"] != journal["incoming_manifest_sha256"]
        ):
            _fail("durable incoming release does not match the residual journal")
        if (
            _state_tree_fingerprint(self.paths.model_guard_state_root)
            != journal["persistent_state_fingerprint"]
        ):
            _fail("model-guard persistent state changed after release commit")
        self._validate_transaction_owned_release(
            journal, self.paths.releases / journal["incoming_release_id"]
        )
        validated_state, release, _ = self._validate_current_trust()
        if validated_state["current_release_id"] != journal["incoming_release_id"]:
            _fail("committed release validation selected an unexpected release")
        transaction_path = self.paths.transactions / journal["transaction_id"]
        if transaction_path.exists():
            _remove_private_tree(transaction_path, self.paths.transactions)
        os.unlink(self.paths.journal_file)
        _fsync_directory(self.paths.state_dir)
        return release

    def rollback(self) -> Path:
        with self._lock():
            journal = self._load_journal()
            if journal is None:
                state, release, _ = self._validate_current_trust()
                return release
            previous = journal["previous_release_id"]
            incoming = journal["incoming_release_id"]
            state = self._load_state()
            committed = self._complete_committed_journal(journal, state)
            if committed is not None:
                return committed
            if state["current_release_id"] != previous:
                _fail("cannot rollback: durable previous release state changed")
            target = os.readlink(self.paths.current) if self.paths.current.is_symlink() else ""
            incoming_target = f".releases/{incoming}"
            previous_target = f".releases/{previous}"
            if target not in (incoming_target, previous_target):
                _fail("cannot rollback an unknown active release pointer")
            incoming_path = self._transaction_release_for_cleanup(journal)
            if target == incoming_target and incoming_path is None:
                _fail("cannot stop an active incoming release whose tree is missing")
            if incoming_path is not None:
                self._stop_incoming_before_cleanup(journal, incoming_path)
            if target == incoming_target:
                self._switch_current(previous)
            if incoming_path is not None:
                _remove_private_tree(incoming_path, self.paths.releases)
            transaction_path = self.paths.transactions / journal["transaction_id"]
            if transaction_path.exists():
                _remove_private_tree(transaction_path, self.paths.transactions)
            os.unlink(self.paths.journal_file)
            _fsync_directory(self.paths.state_dir)
            if (
                _state_tree_fingerprint(self.paths.model_guard_state_root)
                != journal["persistent_state_fingerprint"]
            ):
                _fail("model-guard persistent state changed across rollback")
            return self.paths.releases / previous

    def recover(self) -> Path:
        with self._lock():
            journal = self._load_journal()
            if journal is None:
                _, release, _ = self._validate_current_trust()
                return release
            state = self._load_state()
            committed = self._complete_committed_journal(journal, state)
            if committed is not None:
                return committed
            if state["current_release_id"] != journal["previous_release_id"]:
                _fail("interrupted transaction no longer matches durable state")
            target = os.readlink(self.paths.current) if self.paths.current.is_symlink() else ""
            previous_target = f".releases/{journal['previous_release_id']}"
            incoming_target = f".releases/{journal['incoming_release_id']}"
            if target not in (incoming_target, previous_target):
                _fail("interrupted transaction left an unknown active pointer")
            incoming_path = self._transaction_release_for_cleanup(journal)
            if target == incoming_target and incoming_path is None:
                _fail("cannot stop an active incoming release whose tree is missing")
            if incoming_path is not None:
                self._stop_incoming_before_cleanup(journal, incoming_path)
            if target == incoming_target:
                self._switch_current(journal["previous_release_id"])
            if incoming_path is not None:
                _remove_private_tree(incoming_path, self.paths.releases)
            transaction_path = self.paths.transactions / journal["transaction_id"]
            if transaction_path.exists():
                _remove_private_tree(transaction_path, self.paths.transactions)
            os.unlink(self.paths.journal_file)
            _fsync_directory(self.paths.state_dir)
            if (
                _state_tree_fingerprint(self.paths.model_guard_state_root)
                != journal["persistent_state_fingerprint"]
            ):
                _fail("model-guard persistent state changed across recovery")
            return self.paths.releases / journal["previous_release_id"]

    def active_path(self) -> Path:
        with self._lock():
            _, release, _ = self._validate_current_trust()
            return release

    def pending_path(self) -> Path:
        with self._lock():
            return self._pending_release_for_health()

    def pending_health_script(self) -> Path:
        """Return the trusted active-release health evaluator for the pending switch."""
        with self._lock():
            self._pending_release_for_health()
            health_script, expected_sha256 = (
                self._trusted_pending_health_script()
            )
            with self._pinned_health_script(
                health_script, expected_sha256
            ):
                return health_script

    def run_pending_health(self, runner_pid_value: str, expected_release_value: Path) -> None:
        """Run the trusted active-release health gate for the switched candidate."""
        if re.fullmatch(r"[0-9]+", runner_pid_value) is None:
            _fail("pending health runner PID rejected")
        runner_pid = int(runner_pid_value)
        if runner_pid < 1 or runner_pid > (1 << 31) - 1:
            _fail("pending health runner PID rejected")
        expected_text = os.fspath(expected_release_value)
        if not expected_text or expected_text != os.path.abspath(expected_text):
            _fail("pending health release path must be absolute and canonical")
        expected_release = Path(expected_text)

        with self._lock():
            release = self._pending_release_for_health()
            if release != expected_release:
                _fail("pending health release path does not match the transaction")
            health_script, expected_sha256 = (
                self._trusted_pending_health_script()
            )

        try:
            with self._pinned_health_script(
                health_script, expected_sha256
            ) as (health_fd_path, health_fd):
                health = subprocess.Popen(
                    (str(health_fd_path), str(runner_pid), str(release)),
                    stdin=subprocess.DEVNULL,
                    close_fds=True,
                    pass_fds=(health_fd,),
                    env={"LC_ALL": "C", "PATH": "/usr/sbin:/usr/bin:/sbin:/bin"},
                    start_new_session=True,
                )
                try:
                    return_code = health.wait(
                        timeout=CANDIDATE_HEALTH_TIMEOUT_SECONDS
                    )
                except subprocess.TimeoutExpired as error:
                    try:
                        os.killpg(health.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                    except OSError as stop_error:
                        raise ReleaseError(
                            "timed-out trusted health process group could not be stopped"
                        ) from stop_error
                    try:
                        health.wait(timeout=HEALTH_RUNNER_STOP_TIMEOUT_SECONDS)
                    except subprocess.TimeoutExpired as stop_error:
                        raise ReleaseError(
                            "timed-out trusted health process could not be reaped"
                        ) from stop_error
                    raise ReleaseError(
                        f"pending release health check exceeded the trusted "
                        f"{CANDIDATE_HEALTH_TIMEOUT_SECONDS}s timeout"
                    ) from error
        except OSError as error:
            raise ReleaseError("trusted pending health check could not start") from error
        if return_code != 0:
            _fail("pending candidate did not pass the trusted active-release health check")

        # The trusted evaluator ran outside the updater lock.  Re-check the exact
        # switched transaction before allowing the trusted caller to commit it.
        with self._lock():
            current_release = self._pending_release_for_health()
            if current_release != release:
                _fail("pending release changed during health validation")
            current_health, current_sha256 = self._trusted_pending_health_script()
            if (
                current_health != health_script
                or current_sha256 != expected_sha256
            ):
                _fail("trusted health evaluator changed during validation")
            with self._pinned_health_script(
                current_health, current_sha256
            ):
                pass

    def _pending_release_for_health(self) -> Path:
        journal = self._load_journal()
        if journal is None or journal["phase"] != "switched":
            _fail("no switched release is pending health validation")
        state = self._load_state()
        if state["current_release_id"] != journal["previous_release_id"]:
            _fail("pending release no longer matches durable previous state")
        try:
            current_info = os.lstat(self.paths.current)
        except OSError as error:
            raise ReleaseError("pending release pointer is unavailable") from error
        expected_target = f".releases/{journal['incoming_release_id']}"
        if (
            not stat.S_ISLNK(current_info.st_mode)
            or os.readlink(self.paths.current) != expected_target
        ):
            _fail("pending release pointer rejected")
        release = self._transaction_release_for_cleanup(journal)
        if release is None:
            _fail("pending release publication is incomplete")
        meta = release / "meta"
        _directory(meta)
        manifest_bytes = _read_exact(
            meta / "compatibility.manifest.json",
            MAX_MANIFEST_BYTES,
        )
        if _sha256_bytes(manifest_bytes) != journal["incoming_manifest_sha256"]:
            _fail("pending release manifest changed before health validation")
        if (
            _state_tree_fingerprint(self.paths.model_guard_state_root)
            != journal["persistent_state_fingerprint"]
        ):
            _fail("model-guard persistent state changed before health validation")
        return release

    def _load_bootstrap_journal(self) -> Mapping[str, Any] | None:
        if not self.paths.bootstrap_journal_file.exists():
            return None
        data = _read_exact(
            self.paths.bootstrap_journal_file,
            MAX_MANIFEST_BYTES,
        )
        value = _strict_json(data, MAX_MANIFEST_BYTES, "release bootstrap journal")
        journal = _require_object(
            value,
            {
                "format",
                "incoming_manifest_sha256",
                "incoming_release_id",
                "persistent_state_fingerprint",
                "phase",
                "transaction_id",
            },
            "release bootstrap journal",
        )
        if journal["format"] != BOOTSTRAP_JOURNAL_FORMAT:
            _fail("release bootstrap journal format rejected")
        _require_string(journal["transaction_id"], "bootstrap transaction ID", r"[0-9a-f]{32}")
        _validate_release_id(journal["incoming_release_id"])
        _require_hex(
            journal["incoming_manifest_sha256"],
            64,
            "bootstrap journal manifest sha256",
            nonzero=True,
        )
        _require_hex(
            journal["persistent_state_fingerprint"],
            64,
            "bootstrap journal persistent-state fingerprint",
        )
        if journal["phase"] not in (
            "preparing",
            "staged",
            "stopped",
            "migrating",
            "switched",
            "healthy",
        ):
            _fail("release bootstrap journal phase rejected")
        return journal

    def _write_bootstrap_journal(self, journal: Mapping[str, Any]) -> None:
        _atomic_write(
            self.paths.bootstrap_journal_file,
            _canonical_json(dict(journal)),
            0o600,
        )

    def _validate_legacy_facades(self) -> None:
        for name in FACADE_DIRECTORIES:
            candidate = self.paths.install_root / name
            try:
                info = os.lstat(candidate)
            except OSError as error:
                raise ReleaseError(f"legacy release facade is unavailable: {name}") from error
            if not stat.S_ISDIR(info.st_mode):
                _fail(f"legacy release facade is not a directory: {name}")

    def _preflight_facade_exchange(self) -> None:
        """Prove renameat2(RENAME_EXCHANGE) before stopping the workload."""
        first = self.paths.state_dir / f".exchange-a-{uuid.uuid4().hex}"
        second = self.paths.state_dir / f".exchange-b-{uuid.uuid4().hex}"
        os.symlink("a", first)
        try:
            os.symlink("b", second)
            _fsync_directory(self.paths.state_dir)
            _rename_exchange(first, second)
            if os.readlink(first) != "b" or os.readlink(second) != "a":
                _fail("atomic facade exchange preflight returned an invalid result")
        finally:
            with contextlib.suppress(OSError):
                os.unlink(first)
            with contextlib.suppress(OSError):
                os.unlink(second)
            _fsync_directory(self.paths.state_dir)

    def _validate_signed_candidate_script(self, release: Path, name: str) -> Path:
        if name not in SIGNED_CANDIDATE_SCRIPT_NAMES:
            _fail("signed candidate script name rejected")
        if release.parent != self.paths.releases:
            _fail("signed candidate escaped the protected release directory")
        _validate_release_id(release.name)
        _directory(release)
        scripts = release / "scripts"
        _directory(scripts)
        script = release / "scripts" / name
        try:
            _regular_file(script)
        except ReleaseError as error:
            raise ReleaseError(
                f"signed candidate script metadata rejected: {name}"
            ) from error
        return script

    def _trusted_pending_health_script(self) -> tuple[Path, str]:
        journal = self._load_journal()
        if journal is None or journal["phase"] != "switched":
            _fail("no switched release has a trusted health evaluator")
        state = self._load_state()
        if state["current_release_id"] != journal["previous_release_id"]:
            _fail("trusted health evaluator no longer matches durable state")
        trusted_release = self.paths.releases / journal["previous_release_id"]
        manifest = self._validate_release_trust_against_state(
            state, trusted_release
        )
        payload_bytes = _read_exact(
            trusted_release / "meta/payload.files.json",
            MAX_PAYLOAD_MANIFEST_BYTES,
        )
        if _sha256_bytes(payload_bytes) != manifest["payload_manifest_sha256"]:
            _fail("trusted active-release payload manifest digest mismatch")
        entries = _validate_payload_manifest(
            _strict_json(
                payload_bytes,
                MAX_PAYLOAD_MANIFEST_BYTES,
                "trusted active-release payload manifest",
            )
        )
        matches = [
            entry
            for entry in entries
            if entry["path"] == "scripts/release_health_check.sh"
        ]
        if len(matches) != 1:
            _fail("trusted active release has no unique health evaluator")
        entry = matches[0]
        if (
            entry["type"] != "file"
            or entry["mode"] != 0o755
            or entry["size"] < 1
            or entry["size"] > MAX_HEALTH_SCRIPT_BYTES
        ):
            _fail("trusted active-release health evaluator metadata rejected")
        health_script = self._validate_signed_candidate_script(
            trusted_release, "release_health_check.sh"
        )
        return health_script, str(entry["sha256"])

    def _stable_bootstrap_health_script(self) -> Path:
        stable_root = self.paths.install_root / ".release-bootstrap"
        scripts = stable_root / "scripts"
        _directory(stable_root)
        _directory(scripts)
        health_script = self.paths.stable_health_script
        _regular_file(health_script, MAX_HEALTH_SCRIPT_BYTES)
        return health_script

    @contextlib.contextmanager
    def _pinned_health_script(
        self, path: Path, expected_sha256: str | None
    ) -> Iterator[tuple[Path, int]]:
        initial = _regular_file(path, MAX_HEALTH_SCRIPT_BYTES)
        if initial.st_size < 1:
            _fail("trusted health evaluator is empty")
        descriptor = os.open(path, os.O_RDONLY | os.O_CLOEXEC)
        try:
            opened = os.fstat(descriptor)
            if not _same_file_snapshot(initial, opened):
                _fail("trusted health evaluator changed while opening")
            digest = hashlib.sha256()
            offset = 0
            while offset < initial.st_size:
                block = os.pread(
                    descriptor,
                    min(1024 * 1024, initial.st_size - offset),
                    offset,
                )
                if not block:
                    _fail("trusted health evaluator became unreadable")
                digest.update(block)
                offset += len(block)
            try:
                path_after_read = os.stat(path)
            except OSError as error:
                raise ReleaseError(
                    "trusted health evaluator path changed while reading"
                ) from error
            if (
                not _same_file_snapshot(initial, os.fstat(descriptor))
                or not _same_file_snapshot(initial, path_after_read)
            ):
                _fail("trusted health evaluator changed while reading")
            if (
                expected_sha256 is not None
                and digest.hexdigest() != expected_sha256
            ):
                _fail("trusted active-release health evaluator digest mismatch")
            yield Path(f"/proc/self/fd/{descriptor}"), descriptor
            try:
                path_after_run = os.stat(path)
            except OSError as error:
                raise ReleaseError(
                    "trusted health evaluator path changed while running"
                ) from error
            if (
                not _same_file_snapshot(initial, os.fstat(descriptor))
                or not _same_file_snapshot(initial, path_after_run)
            ):
                _fail("trusted health evaluator changed while running")
        finally:
            os.close(descriptor)

    def _stop_incoming_before_cleanup(
        self, journal: Mapping[str, Any], incoming: Path
    ) -> None:
        """Stop an authenticated published candidate before pointer/tree reversal."""
        if journal["phase"] != "switched":
            # A preparing/publishing/staged transaction has not launched the
            # candidate through the controlled start path.  Running its global
            # stop contract here could kill the still-active previous release.
            return
        if not incoming.exists() and not incoming.is_symlink():
            _fail("published incoming release tree is missing before stop")
        self._run_signed_candidate_stop(incoming)
        if (
            _state_tree_fingerprint(self.paths.model_guard_state_root)
            != journal["persistent_state_fingerprint"]
        ):
            _fail("model-guard persistent state changed while stopping candidate")

    def _open_bootstrap_log(self, path: Path) -> int:
        if path.parent != self.paths.state_dir:
            _fail("bootstrap log path escaped the protected release state directory")
        _directory(self.paths.state_dir)
        descriptor = os.open(
            path,
            os.O_WRONLY | os.O_APPEND | os.O_CREAT | os.O_CLOEXEC | os.O_NOFOLLOW,
            0o600,
        )
        info = os.fstat(descriptor)
        if not stat.S_ISREG(info.st_mode):
            os.close(descriptor)
            _fail("bootstrap health log is not a regular file")
        return descriptor

    def _preflight_bootstrap_health_gate(self, release: Path) -> BootstrapHealthPlan:
        """Resolve every health-gate dependency before the signed stop boundary."""
        plan = BootstrapHealthPlan(
            run_script=self._validate_signed_candidate_script(release, "run_start.sh"),
            health_script=self._stable_bootstrap_health_script(),
            stop_script=self._validate_signed_candidate_script(release, "stop.sh"),
            log_path=self.paths.state_dir / "release-bootstrap.log",
        )
        descriptor = self._open_bootstrap_log(plan.log_path)
        os.close(descriptor)
        return plan

    def _run_signed_candidate_stop(self, release: Path) -> None:
        stop_script = self._validate_signed_candidate_script(release, "stop.sh")
        try:
            result = subprocess.run(
                (str(stop_script),),
                cwd=release / "scripts",
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                check=False,
                close_fds=True,
                env={
                    "COSMO_STOP_TIMEOUT_SECONDS": "15",
                    "INSTALLPATH": str(release),
                    "LC_ALL": "C",
                    "PATH": "/usr/sbin:/usr/bin:/sbin:/bin",
                },
                timeout=30,
            )
        except (OSError, subprocess.TimeoutExpired) as error:
            raise ReleaseError("signed candidate stop operation failed") from error
        if result.returncode != 0:
            _fail("signed candidate stop script did not complete successfully")
        remaining = self._managed_processes_remaining()
        if remaining:
            _fail(
                "managed processes remain after signed candidate stop: "
                + ", ".join(remaining)
            )

    @staticmethod
    def _managed_processes_remaining() -> list[str]:
        remaining: set[str] = set()
        try:
            process_entries = Path("/proc").iterdir()
            for entry in process_entries:
                if not entry.name.isdecimal():
                    continue
                try:
                    name = (entry / "comm").read_text(encoding="ascii").strip()
                    process_stat = (entry / "stat").read_text(encoding="ascii")
                except (OSError, UnicodeError):
                    continue
                closing_parenthesis = process_stat.rfind(")")
                state_offset = closing_parenthesis + 2
                if (
                    name in MANAGED_PROCESS_NAMES
                    and closing_parenthesis >= 0
                    and state_offset < len(process_stat)
                    and process_stat[state_offset] != "Z"
                ):
                    remaining.add(name)
        except OSError as error:
            raise ReleaseError("cannot inspect managed processes after stop") from error
        return sorted(remaining)

    @staticmethod
    def _process_owns_tcp_listener(process_id: int, port: int) -> bool:
        socket_inodes: set[str] = set()
        try:
            descriptors = Path(f"/proc/{process_id}/fd").iterdir()
            for descriptor in descriptors:
                with contextlib.suppress(OSError):
                    target = os.readlink(descriptor)
                    match = re.fullmatch(r"socket:\[(\d+)\]", target)
                    if match is not None:
                        socket_inodes.add(match.group(1))
        except OSError:
            return False
        if not socket_inodes:
            return False
        expected_port = f"{port:04X}"
        for table_path in (Path("/proc/net/tcp"), Path("/proc/net/tcp6")):
            try:
                lines = table_path.read_text(encoding="ascii").splitlines()[1:]
            except (OSError, UnicodeError):
                continue
            for line in lines:
                fields = line.split()
                if (
                    len(fields) >= 10
                    and fields[1].upper().endswith(f":{expected_port}")
                    and fields[3] == "0A"
                    and fields[9] in socket_inodes
                ):
                    return True
        return False

    def _legacy_restart_ready(self, runner: subprocess.Popen[bytes], expected_engine: Path) -> bool:
        if runner.poll() is not None:
            return False
        expected = os.path.realpath(expected_engine)
        matching: list[int] = []
        try:
            process_entries = Path("/proc").iterdir()
            for entry in process_entries:
                if not entry.name.isdecimal():
                    continue
                with contextlib.suppress(OSError):
                    if os.path.realpath(entry / "exe") == expected:
                        matching.append(int(entry.name))
        except OSError:
            return False
        return len(matching) == 1 and self._process_owns_tcp_listener(matching[0], 8000)

    def _stop_signed_runner(
        self, release: Path, runner: subprocess.Popen[bytes], description: str
    ) -> None:
        stop_error: BaseException | None = None
        try:
            self._run_signed_candidate_stop(release)
        except BaseException as error:
            stop_error = error
        if runner.poll() is None:
            with contextlib.suppress(subprocess.TimeoutExpired):
                runner.wait(timeout=HEALTH_RUNNER_STOP_TIMEOUT_SECONDS)
        if runner.poll() is None:
            with contextlib.suppress(ProcessLookupError):
                os.killpg(runner.pid, signal.SIGTERM)
            with contextlib.suppress(subprocess.TimeoutExpired):
                runner.wait(timeout=HEALTH_RUNNER_STOP_TIMEOUT_SECONDS)
        if runner.poll() is None:
            with contextlib.suppress(ProcessLookupError):
                os.killpg(runner.pid, signal.SIGKILL)
            with contextlib.suppress(subprocess.TimeoutExpired):
                runner.wait(timeout=HEALTH_RUNNER_STOP_TIMEOUT_SECONDS)
        if stop_error is not None or runner.poll() is None:
            raise ReleaseError(
                f"signed candidate could not stop the {description} cleanly"
            ) from stop_error

    def _run_legacy_restart_process(self, release: Path) -> None:
        run_script = self._validate_signed_candidate_script(release, "run_start.sh")
        stop_script = self._validate_signed_candidate_script(release, "stop.sh")
        expected_engine = self.paths.install_root / "bin/cosmo-engine"
        engine_info = os.lstat(expected_engine)
        if (
            not stat.S_ISREG(engine_info.st_mode)
            or not stat.S_IMODE(engine_info.st_mode) & 0o111
        ):
            _fail("restored legacy engine is not executable")
        log_path = self.paths.state_dir / "release-bootstrap.log"
        log_descriptor = self._open_bootstrap_log(log_path)
        runner: subprocess.Popen[bytes] | None = None
        try:
            runner = subprocess.Popen(
                (str(run_script), "start", str(log_path)),
                cwd=release / "scripts",
                stdin=subprocess.DEVNULL,
                stdout=log_descriptor,
                stderr=log_descriptor,
                close_fds=True,
                env={
                    "COSMO_TRUSTED_STOP_SCRIPT": str(stop_script),
                    "INSTALLPATH": str(self.paths.install_root),
                    "LC_ALL": "C",
                    "PATH": "/usr/sbin:/usr/bin:/sbin:/bin",
                },
                start_new_session=True,
            )
        except OSError as error:
            raise ReleaseError("signed candidate could not start the restored legacy workload") from error
        finally:
            os.close(log_descriptor)
        assert runner is not None
        deadline = time.monotonic() + LEGACY_RESTART_TIMEOUT_SECONDS
        while time.monotonic() < deadline:
            if self._legacy_restart_ready(runner, expected_engine):
                return
            if runner.poll() is not None:
                break
            time.sleep(1)
        self._stop_signed_runner(release, runner, "restored legacy workload")
        _fail("restored legacy workload did not pass the fixed restart gate")

    def _restart_legacy_workload(self, release: Path) -> None:
        self._lifecycle("bootstrap_legacy_restart_attempted")
        self._run_legacy_restart_process(release)
        self._lifecycle("bootstrap_legacy_restart_succeeded")

    def _recover_bootstrap_locked(self, journal: Mapping[str, Any]) -> Path | None:
        release_id = str(journal["incoming_release_id"])
        transaction_id = str(journal["transaction_id"])
        staged = self.paths.releases / release_id
        if self.paths.state_file.exists():
            state, release, _ = self._validate_current_trust()
            if (
                state["current_release_id"] != release_id
                or state["manifest_sha256"] != journal["incoming_manifest_sha256"]
            ):
                _fail("bootstrap state and journal describe different releases")
            transaction = self.paths.transactions / transaction_id
            if transaction.exists():
                _remove_private_tree(transaction, self.paths.transactions)
            os.unlink(self.paths.bootstrap_journal_file)
            _fsync_directory(self.paths.state_dir)
            return release

        if journal["phase"] in ("switched", "healthy"):
            self._run_signed_candidate_stop(staged)

        for name in FACADE_DIRECTORIES:
            facade = self.paths.install_root / name
            backup = self.paths.legacy_backup / name
            facade_present = facade.exists() or facade.is_symlink()
            backup_present = backup.exists() or backup.is_symlink()
            if backup_present:
                backup_info = os.lstat(backup)
                if stat.S_ISLNK(backup_info.st_mode):
                    if (
                        os.readlink(backup) != f"current/{name}"
                        or not facade_present
                    ):
                        _fail(f"bootstrap pre-exchange facade marker rejected: {name}")
                    facade_info = os.lstat(facade)
                    if not stat.S_ISDIR(facade_info.st_mode):
                        _fail(f"bootstrap pre-exchange legacy facade rejected: {name}")
                    os.unlink(backup)
                    backup_present = False
            if backup_present:
                backup_info = os.lstat(backup)
                facade_info = os.lstat(facade)
                if (
                    not stat.S_ISDIR(backup_info.st_mode)
                    or not stat.S_ISLNK(facade_info.st_mode)
                    or os.readlink(facade) != f"current/{name}"
                ):
                    _fail(f"bootstrap legacy backup rejected: {name}")
                # The exchange restores the legacy facade without a single
                # pathname gap.  If power fails after it, the marker branch
                # above removes the signed symlink left in the backup slot.
                _rename_exchange(facade, backup)
                _fsync_directory(self.paths.install_root)
                _fsync_directory(self.paths.legacy_backup)
                os.unlink(backup)
                _fsync_directory(self.paths.legacy_backup)
            elif facade_present:
                facade_info = os.lstat(facade)
                if not stat.S_ISDIR(facade_info.st_mode):
                    _fail(f"bootstrap recovery legacy facade rejected: {name}")
            else:
                _fail(f"bootstrap recovery lost facade: {name}")

        current = self.paths.current
        if current.exists() or current.is_symlink():
            info = os.lstat(current)
            if (
                not stat.S_ISLNK(info.st_mode)
                or os.readlink(current) != f".releases/{release_id}"
            ):
                _fail("cannot recover a replaced bootstrap current pointer")
            os.unlink(current)

        _fsync_directory(self.paths.install_root)
        if self.paths.legacy_backup.exists():
            _fsync_directory(self.paths.legacy_backup)

        # Every phase at or beyond staging may have entered the signed stop
        # operation.  Restart the restored legacy workload with candidate-signed
        # orchestration before deleting the only authenticated scripts.
        if journal["phase"] != "preparing":
            if not staged.exists():
                _fail("bootstrap recovery lost the signed restart candidate")
            self._restart_legacy_workload(staged)

        if staged.exists():
            _remove_private_tree(staged, self.paths.releases)
        transaction = self.paths.transactions / transaction_id
        if transaction.exists():
            _remove_private_tree(transaction, self.paths.transactions)
        if self.paths.legacy_backup.exists():
            try:
                os.rmdir(self.paths.legacy_backup)
            except OSError as error:
                raise ReleaseError("bootstrap legacy backup did not recover completely") from error
        os.unlink(self.paths.bootstrap_journal_file)
        _fsync_directory(self.paths.state_dir)
        return None

    def _run_bootstrap_health_gate(
        self, release: Path, plan: BootstrapHealthPlan
    ) -> None:
        log_fd = self._open_bootstrap_log(plan.log_path)
        runner: subprocess.Popen[bytes] | None = None
        health_error: BaseException | None = None
        healthy = False
        try:
            environment = {
                "INSTALLPATH": str(release),
                "LC_ALL": "C",
                "LD_LIBRARY_PATH": f"{release}/lib:/usr/lib",
                "PATH": "/usr/sbin:/usr/bin:/sbin:/bin",
            }
            with self._pinned_health_script(
                plan.health_script, None
            ) as (health_fd_path, health_fd):
                runner = subprocess.Popen(
                    (str(plan.run_script), "start", str(plan.log_path)),
                    cwd=release / "scripts",
                    stdin=subprocess.DEVNULL,
                    stdout=log_fd,
                    stderr=log_fd,
                    close_fds=True,
                    env=environment,
                    start_new_session=True,
                )
                health = subprocess.run(
                    (str(health_fd_path), str(runner.pid), str(release)),
                    stdin=subprocess.DEVNULL,
                    stdout=log_fd,
                    stderr=log_fd,
                    check=False,
                    close_fds=True,
                    pass_fds=(health_fd,),
                    env={
                        "LC_ALL": "C",
                        "PATH": "/usr/sbin:/usr/bin:/sbin:/bin",
                    },
                    timeout=90,
                )
                healthy = health.returncode == 0 and runner.poll() is None
        except (OSError, ReleaseError, subprocess.TimeoutExpired) as error:
            health_error = error
        finally:
            os.close(log_fd)
        if runner is not None:
            # A health probe is never the long-lived service instance.  Stop it
            # before committing state so a successful stable installer cannot
            # leave an orphan outside systemd's lifecycle.
            self._stop_signed_runner(release, runner, "bootstrap health probe")
        if health_error is not None:
            raise ReleaseError("bootstrap candidate health validation failed") from health_error
        if not healthy:
            _fail("bootstrap candidate did not pass the fixed health gate")

    def bootstrap_from_embedded_verifier(
        self,
        archive_fd: int,
        authenticated_manifest: bytes,
        authenticated_signature: bytes,
        public_key: bytes,
        expected_raw_key: bytes,
        expected_key_id: bytes,
        expected_pem_sha256: bytes,
    ) -> Path:
        """Install the first signed release after the C++ embedded-key check.

        This is a backend API, not a command-line operation. The production
        backend supplies an already-open archive FD and authenticates its peer
        as the fixed C++ verifier before invoking this method.
        """
        if (
            len(authenticated_manifest) == 0
            or len(authenticated_manifest) > MAX_MANIFEST_BYTES
            or len(authenticated_signature) != 64
            or len(expected_raw_key) != 32
            or len(expected_key_id) != 16
            or len(expected_pem_sha256) != 32
            or len(public_key) == 0
            or len(public_key) > 16 * 1024
        ):
            _fail("embedded verifier bootstrap input size rejected")
        if hashlib.sha256(public_key).digest() != expected_pem_sha256:
            _fail("embedded verifier PEM digest mismatch")
        derived_key_id = hashlib.sha256(
            b"cosmo-release-key-id-v1" + (1).to_bytes(2, "big") + expected_raw_key
        ).digest()[:16]
        if derived_key_id != expected_key_id or not any(expected_key_id):
            _fail("embedded verifier release key ID mismatch")

        archive_info = os.fstat(archive_fd)
        if (
            not stat.S_ISREG(archive_info.st_mode)
            or archive_info.st_size <= 0
            or archive_info.st_size > MAX_ARCHIVE_BYTES
        ):
            _fail("embedded verifier archive FD type or size rejected")

        with self._lock():
            recovered = self._load_bootstrap_journal()
            if recovered is not None:
                committed = self._recover_bootstrap_locked(recovered)
                if committed is not None:
                    return committed
            if (
                self.paths.state_file.exists()
                or self.paths.current.exists()
                or self.paths.current.is_symlink()
            ):
                _fail("release trust is already initialized; use the ordinary updater")
            if self._load_journal() is not None:
                _fail("an ordinary release transaction already exists")
            if any(self.paths.releases.iterdir()) or any(self.paths.transactions.iterdir()):
                _fail("bootstrap release storage is not empty")
            if self.paths.legacy_backup.exists() or self.paths.legacy_backup.is_symlink():
                _fail("bootstrap legacy backup path already exists")
            self._validate_legacy_facades()

            persistent_fingerprint = _state_tree_fingerprint(
                self.paths.model_guard_state_root
            )
            transaction_id = uuid.uuid4().hex
            transaction_root = self.paths.transactions / transaction_id
            journal: dict[str, Any] = {
                "format": BOOTSTRAP_JOURNAL_FORMAT,
                "incoming_manifest_sha256": _sha256_bytes(authenticated_manifest),
                "incoming_release_id": "pending",
                "persistent_state_fingerprint": persistent_fingerprint,
                "phase": "preparing",
                "transaction_id": transaction_id,
            }
            # The pending value is schema-valid and is replaced by the signed
            # release ID before any compatibility facade is moved.
            self._write_bootstrap_journal(journal)
            self._interrupt("bootstrap_after_journal")
            transaction_root.mkdir(mode=0o700)
            controlled_archive = transaction_root / "signed-release.tar.gz"
            destination_fd = os.open(
                controlled_archive,
                os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC | os.O_NOFOLLOW,
                0o600,
            )
            try:
                os.lseek(archive_fd, 0, os.SEEK_SET)
                copied = 0
                while True:
                    block = os.read(archive_fd, 1024 * 1024)
                    if not block:
                        break
                    copied += len(block)
                    if copied > archive_info.st_size:
                        _fail("bootstrap archive grew while copying")
                    offset = 0
                    while offset < len(block):
                        offset += os.write(destination_fd, block[offset:])
                if copied != archive_info.st_size:
                    _fail("bootstrap archive size changed while copying")
                os.fdatasync(destination_fd)
            finally:
                os.close(destination_fd)
            current_archive_info = os.fstat(archive_fd)
            if (
                current_archive_info.st_dev,
                current_archive_info.st_ino,
                current_archive_info.st_size,
                current_archive_info.st_mtime_ns,
                current_archive_info.st_ctime_ns,
            ) != (
                archive_info.st_dev,
                archive_info.st_ino,
                archive_info.st_size,
                archive_info.st_mtime_ns,
                archive_info.st_ctime_ns,
            ):
                _fail("bootstrap archive inode changed during verification")

            trust_root = transaction_root / "embedded-trust/meta"
            trust_root.mkdir(mode=0o700, parents=True)
            trusted_key = trust_root / "release-public-key.pem"
            key_fd = os.open(
                trusted_key,
                os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC | os.O_NOFOLLOW,
                0o600,
            )
            try:
                offset = 0
                while offset < len(public_key):
                    offset += os.write(key_fd, public_key[offset:])
                os.fdatasync(key_fd)
            finally:
                os.close(key_fd)
            _fsync_directory(trust_root)

            bootstrap_state = {
                "release_generation": 0,
                "release_key_id": expected_key_id.hex(),
                "release_public_key_sha256": expected_pem_sha256.hex(),
            }
            inspection = self._inspect_archive(
                controlled_archive,
                bootstrap_state,
                transaction_root / "embedded-trust",
                {"release_id": "bootstrap-anchor"},
            )
            if (
                inspection.manifest_bytes != authenticated_manifest
                or inspection.signature != authenticated_signature
            ):
                _fail("C++-authenticated metadata differs from the controlled archive")
            if (
                inspection.manifest["release_key"]["id"] != expected_key_id.hex()
                or inspection.manifest["release_key"]["public_key_sha256"]
                != expected_pem_sha256.hex()
            ):
                _fail("signed manifest does not select the embedded release key")

            release_id = str(inspection.manifest["release_id"])
            journal["incoming_release_id"] = release_id
            self._write_bootstrap_journal(journal)
            work_root = transaction_root / "work"
            extracted_root = self._extract_archive(controlled_archive, inspection, work_root)
            payload_root = extracted_root / "payload"
            meta_root = extracted_root / "meta"
            os.rename(meta_root, payload_root / "meta")
            installed_key = payload_root / "meta/release-public-key.pem"
            installed_key_fd = os.open(
                installed_key,
                os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC | os.O_NOFOLLOW,
                0o600,
            )
            try:
                offset = 0
                while offset < len(public_key):
                    offset += os.write(installed_key_fd, public_key[offset:])
                os.fdatasync(installed_key_fd)
            finally:
                os.close(installed_key_fd)
            _fsync_directory(payload_root / "meta")

            _check_release_layout(payload_root)
            _check_component_hashes(payload_root, inspection.manifest)
            _scan_preset_models(payload_root)
            if (
                _state_tree_fingerprint(self.paths.model_guard_state_root)
                != persistent_fingerprint
            ):
                _fail("model-guard state changed during bootstrap preparation")

            staged = self.paths.releases / release_id
            if staged.exists() or staged.is_symlink():
                _fail("bootstrap release ID already exists")
            _rename_noreplace(payload_root, staged)
            _fsync_directory(self.paths.releases)
            journal["phase"] = "staged"
            self._write_bootstrap_journal(journal)
            self._interrupt("bootstrap_after_staged")

            # Resolve all authenticated scripts, the protected log inode, and
            # the required kernel primitive while the legacy workload and all
            # of its paths are still intact.
            health_plan = self._preflight_bootstrap_health_gate(staged)
            self._preflight_facade_exchange()

            # Stop the legacy workload with code from the authenticated,
            # extracted candidate before moving even one legacy facade.  No
            # unsigned legacy script participates in this trust transition.
            self._run_signed_candidate_stop(staged)
            journal["phase"] = "stopped"
            self._write_bootstrap_journal(journal)
            self._interrupt("bootstrap_after_stop")

            self.paths.legacy_backup.mkdir(mode=0o700)
            journal["phase"] = "migrating"
            self._write_bootstrap_journal(journal)
            self._switch_current(release_id)
            for name in FACADE_DIRECTORIES:
                facade = self.paths.install_root / name
                backup = self.paths.legacy_backup / name
                os.symlink(f"current/{name}", backup)
                _fsync_directory(self.paths.legacy_backup)
                _rename_exchange(backup, facade)
                facade_info = os.lstat(facade)
                backup_info = os.lstat(backup)
                if (
                    not stat.S_ISLNK(facade_info.st_mode)
                    or os.readlink(facade) != f"current/{name}"
                    or not stat.S_ISDIR(backup_info.st_mode)
                ):
                    _fail(f"per-facade atomic bootstrap migration rejected: {name}")
                _fsync_directory(self.paths.legacy_backup)
                _fsync_directory(self.paths.install_root)
                self._interrupt(f"bootstrap_after_facade_{name}")
            journal["phase"] = "switched"
            self._write_bootstrap_journal(journal)
            self._interrupt("bootstrap_after_switch")

            try:
                self._run_bootstrap_health_gate(staged, health_plan)
                if (
                    _state_tree_fingerprint(self.paths.model_guard_state_root)
                    != persistent_fingerprint
                ):
                    _fail("model-guard state changed during bootstrap health validation")
                journal["phase"] = "healthy"
                self._write_bootstrap_journal(journal)
                self._interrupt("bootstrap_after_health")
                state = {
                    "current_release_id": release_id,
                    "format": STATE_FORMAT,
                    "manifest_sha256": _sha256_bytes(inspection.manifest_bytes),
                    "release_generation": inspection.manifest["release_generation"],
                    "release_key_id": inspection.manifest["release_key"]["id"],
                    "release_public_key_sha256": inspection.manifest["release_key"][
                        "public_key_sha256"
                    ],
                }
                _atomic_write(
                    self.paths.state_file,
                    _canonical_json(state),
                    0o600,
                )
                self._interrupt("bootstrap_after_state")
                _remove_private_tree(transaction_root, self.paths.transactions)
                os.unlink(self.paths.bootstrap_journal_file)
                _fsync_directory(self.paths.state_dir)
                return staged
            except BaseException:
                current_journal = self._load_bootstrap_journal()
                if current_journal is not None:
                    committed = self._recover_bootstrap_locked(current_journal)
                    if committed is not None:
                        return committed
                raise

    def recover_failed_bootstrap(self) -> Path | None:
        """Recover the one fixed-root bootstrap journal after a rejected run."""
        with self._lock():
            journal = self._load_bootstrap_journal()
            if journal is None:
                return None
            return self._recover_bootstrap_locked(journal)

def _production_main(argv: Sequence[str]) -> int:
    parser = argparse.ArgumentParser(description="Apply a signed Cosmo compatibility release")
    subparsers = parser.add_subparsers(dest="command", required=True)
    prepare = subparsers.add_parser("prepare")
    prepare.add_argument("archive")
    subparsers.add_parser("activate")
    subparsers.add_parser("commit-healthy")
    subparsers.add_parser("rollback")
    subparsers.add_parser("recover")
    subparsers.add_parser("active-path")
    subparsers.add_parser("pending-path")
    subparsers.add_parser("pending-health-script")
    run_pending_health = subparsers.add_parser("run-pending-health")
    run_pending_health.add_argument("runner_pid")
    run_pending_health.add_argument("expected_release")
    arguments = parser.parse_args(argv)
    updater = ReleaseUpdater(PRODUCTION_PATHS)
    try:
        if arguments.command == "prepare":
            manifest = updater.prepare(Path(arguments.archive))
            print(manifest["release_id"])
        elif arguments.command == "activate":
            print(updater.activate())
        elif arguments.command == "commit-healthy":
            manifest = updater.commit_healthy()
            print(manifest["release_id"])
        elif arguments.command == "rollback":
            print(updater.rollback())
        elif arguments.command == "recover":
            print(updater.recover())
        elif arguments.command == "active-path":
            print(updater.active_path())
        elif arguments.command == "pending-path":
            print(updater.pending_path())
        elif arguments.command == "pending-health-script":
            print(updater.pending_health_script())
        elif arguments.command == "run-pending-health":
            updater.run_pending_health(
                arguments.runner_pid, Path(arguments.expected_release)
            )
        else:
            _fail("unsupported release updater command")
    except ReleaseError as error:
        print(f"release updater rejected operation: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    if sys.flags.isolated != 1:
        print(
            "release updater must be launched with /usr/bin/python3 -I -B",
            file=sys.stderr,
        )
        raise SystemExit(1)
    raise SystemExit(_production_main(sys.argv[1:]))
