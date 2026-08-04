#!/usr/bin/python3
"""Validate SOURCE and controlled production CPack inventories."""

from __future__ import annotations

import argparse
import hashlib
import importlib.machinery
import importlib.util
import os
import pathlib
import re
import sys
import tarfile
import tempfile
from dataclasses import dataclass
from typing import Mapping


SCRIPT_DIRECTORY = pathlib.Path(__file__).resolve().parent
RELEASE_UPDATER_SOURCE = SCRIPT_DIRECTORY / "release_updater.py"
release_loader = importlib.machinery.SourceFileLoader(
    "cosmo_source_package_release_schema", str(RELEASE_UPDATER_SOURCE)
)
release_spec = importlib.util.spec_from_loader(
    release_loader.name, release_loader
)
if release_spec is None:
    raise RuntimeError("cannot load the shared CEM v2 package scanner")
release_schema = importlib.util.module_from_spec(release_spec)
sys.modules[release_loader.name] = release_schema
release_loader.exec_module(release_schema)


PROFILE_SOURCE = "public-runtime"
PROFILE_PRODUCTION = "production-release"
PROFILES = (PROFILE_SOURCE, PROFILE_PRODUCTION)

COMMON_DIRECTORIES = {
    "bin",
    "files",
    "font",
    "lib",
    "resource",
    "scripts",
    "share/cosmo-model-guard",
    "web",
}
COMMON_FILES = {
    "lib/libcosmo_model_guard.so.2.0.0",
    "share/cosmo-model-guard/cosmo_model_guard_v2.h",
}
COMMON_SYMLINKS = {
    "lib/libcosmo_model_guard.so": "libcosmo_model_guard.so.2",
    "lib/libcosmo_model_guard.so.2": "libcosmo_model_guard.so.2.0.0",
}

SOURCE_EXECUTABLES = {
    "bin/cosmo-engine",
    "install-device.sh",
    "scripts/run_start.sh",
    "scripts/source_health_check.sh",
    "scripts/source_run_start.sh",
    "scripts/stop.sh",
}
SOURCE_FILES = {
    "bin/version.txt",
    "scripts/common.sh",
    "share/cosmo-source/build-identity.env",
    "share/cosmo-source/cosmo.service",
}
SOURCE_BUILD_IDENTITY = "share/cosmo-source/build-identity.env"
SOURCE_BUILD_IDENTITY_FORMAT = "cosmo-source-build-identity-v2"
SOURCE_BUILD_IDENTITY_KEYS = (
    "format",
    "edge_commit",
    "version",
    "engine_sha256",
    "build_identity",
)
SOURCE_ENGINE = "bin/cosmo-engine"
SOURCE_VERSION = "bin/version.txt"
INSPECTED_FILE_LIMITS = {
    SOURCE_BUILD_IDENTITY: 4096,
    SOURCE_ENGINE: 512 * 1024 * 1024,
    SOURCE_VERSION: 1024,
    "share/cosmo-model-guard/cosmo_model_guard_v2.h": 128 * 1024,
    "lib/libcosmo_model_guard.so.2.0.0": 32 * 1024 * 1024,
}
PRIVATE_KEY_PEM_MARKERS = (
    b"-----BEGIN PRIVATE KEY-----",
    b"-----BEGIN ENCRYPTED PRIVATE KEY-----",
    b"-----BEGIN RSA PRIVATE KEY-----",
    b"-----BEGIN EC PRIVATE KEY-----",
    b"-----BEGIN DSA PRIVATE KEY-----",
    b"-----BEGIN OPENSSH PRIVATE KEY-----",
)
PRIVATE_KEY_SCAN_OVERLAP = max(map(len, PRIVATE_KEY_PEM_MARKERS)) - 1

PRODUCTION_EXECUTABLES = {
    ".release-bootstrap/bin/cosmo-release-bootstrap",
    ".release-bootstrap/scripts/release_health_check.sh",
    "bin/cosmo-model-provision",
    "bin/cosmo-release-bootstrap",
    "scripts/install.sh",
    "scripts/inte_run_start.sh",
    "scripts/release_health_check.sh",
    "scripts/release_updater.sh",
    "scripts/start.sh",
}
PRODUCTION_FILES = {
    ".release-bootstrap/lib/libcrypto.so.3",
    ".release-bootstrap/scripts/release_bootstrap_backend.py",
    ".release-bootstrap/scripts/release_updater.py",
    "scripts/release_bootstrap_backend.py",
    "scripts/release_updater.py",
    "share/cosmo-factory/cosmo.service",
}

SOURCE_FORBIDDEN_PREFIXES = (
    ".release-bootstrap/",
    "bin/cosmo-model-provision",
    "bin/cosmo-release-bootstrap",
    "scripts/build_release_",
    "scripts/release_",
    "scripts/verify_release_",
    "share/cosmo-factory/",
    "share/cosmo-model-guard/release-",
)
SOURCE_FORBIDDEN_FILES = PRODUCTION_EXECUTABLES | PRODUCTION_FILES | {
    "scripts/build_release_bundle.py",
    "scripts/build_release_public_key_object.py",
    "scripts/verify_release_public_key_object.py",
    "share/cosmo-model-guard/release-public-key.o",
}
SOURCE_FORBIDDEN_BASENAMES = {
    "commissioning-ed25519.seed",
    "cosmo-model-provision",
    "cosmo-release-bootstrap",
    "device-certificate.bin",
    "product-model-key-v1.bin",
    "product-pepper-v1.bin",
    "product-pepper-v1.o",
    "release-private-key.o",
    "release-public-key.o",
}
SOURCE_PRIVATE_SUFFIXES = (
    ".key",
    ".p8",
    ".p12",
    ".pfx",
    ".pk8",
    ".pkcs12",
    ".jks",
    ".keystore",
    ".private.pem",
)
SOURCE_PRIVATE_NAMED_SUFFIXES = (".der", ".key", ".p8", ".pem", ".pk8")
PRODUCTION_FORBIDDEN_FILES = {
    "install-device.sh",
    "scripts/source_health_check.sh",
    "scripts/source_run_start.sh",
    "share/cosmo-source/build-identity.env",
    "share/cosmo-source/cosmo.service",
}
PRODUCTION_FORBIDDEN_MODEL_GUARD_BASENAMES = {
    "commissioning-ed25519.seed",
    "device-certificate.bin",
    "product-model-key-v1.bin",
    "product-pepper-v1.bin",
}


class PackageAuditError(RuntimeError):
    """Raised when a package does not match its declared profile."""


@dataclass(frozen=True)
class ArchiveEntry:
    kind: str
    mode: int
    linkname: str | None = None
    content: bytes | None = None
    sha256: str | None = None
    preset_cohort_id: str | None = None


def _relative_member(name: str, root: str | None) -> tuple[str, str]:
    path = pathlib.PurePosixPath(name)
    parts = path.parts
    if (
        path.is_absolute()
        or not parts
        or any(part in ("", ".", "..") for part in parts)
    ):
        raise PackageAuditError(f"archive member path is unsafe: {name}")
    package_root = parts[0] if root is None else root
    if parts[0] != package_root:
        raise PackageAuditError("archive must contain exactly one package root")
    relative = pathlib.PurePosixPath(*parts[1:]).as_posix()
    return package_root, "" if relative == "." else relative


def read_inventory(archive: pathlib.Path) -> dict[str, ArchiveEntry]:
    if not archive.is_absolute():
        raise PackageAuditError("package archive path must be absolute")
    if archive.is_symlink() or not archive.is_file():
        raise PackageAuditError("package archive must be one regular file")

    inventory: dict[str, ArchiveEntry] = {}
    package_root: str | None = None
    try:
        with tarfile.open(archive, "r:gz") as package:
            for member in package:
                package_root, relative = _relative_member(
                    member.name, package_root
                )
                if not relative:
                    if not member.isdir():
                        raise PackageAuditError(
                            "package root must be a directory"
                        )
                    continue
                if relative in inventory:
                    raise PackageAuditError(
                        f"duplicate archive member: {relative}"
                    )
                if member.isdir():
                    kind = "directory"
                elif member.isreg():
                    kind = "file"
                elif member.issym():
                    kind = "symlink"
                else:
                    raise PackageAuditError(
                        f"unsupported archive member type: {relative}"
                    )
                content: bytes | None = None
                digest: str | None = None
                preset_cohort_id: str | None = None
                if kind == "file":
                    inspected = relative in INSPECTED_FILE_LIMITS
                    preset_model = (
                        release_schema._is_preset_model_payload_path(relative)
                    )
                    if inspected:
                        maximum_size = INSPECTED_FILE_LIMITS[relative]
                        if member.size < 0 or member.size > maximum_size:
                            raise PackageAuditError(
                                f"package member size is invalid: {relative}"
                            )
                    source = package.extractfile(member)
                    if source is None:
                        raise PackageAuditError(
                            f"cannot read package member: {relative}"
                        )
                    checksum = hashlib.sha256() if inspected else None
                    captured = bytearray()
                    private_key_tail = b""
                    spool = (
                        tempfile.TemporaryFile(
                            prefix="cosmo-source-preset-",
                        )
                        if preset_model
                        else None
                    )
                    try:
                        while True:
                            block = source.read(1024 * 1024)
                            if not block:
                                break
                            scan_block = private_key_tail + block
                            if any(
                                marker in scan_block
                                for marker in PRIVATE_KEY_PEM_MARKERS
                            ):
                                raise PackageAuditError(
                                    "package contains a PEM private key marker: "
                                    f"{relative}"
                                )
                            private_key_tail = scan_block[
                                -PRIVATE_KEY_SCAN_OVERLAP:
                            ]
                            if checksum is not None:
                                checksum.update(block)
                                if relative != SOURCE_ENGINE:
                                    captured.extend(block)
                            if spool is not None:
                                spool.write(block)
                        if spool is not None:
                            spool.flush()
                            os.fchmod(spool.fileno(), 0o400)
                            snapshot = release_schema._validate_cem_v2_core_fd(
                                spool.fileno(),
                                os.fstat(spool.fileno()),
                                relative,
                            )
                            preset_cohort_id = snapshot.cohort_id
                    except release_schema.ReleaseError as error:
                        raise PackageAuditError(
                            f"SOURCE preset model rejected: {relative}: {error}"
                        ) from error
                    finally:
                        source.close()
                        if spool is not None:
                            spool.close()
                    if checksum is not None:
                        digest = checksum.hexdigest()
                        if relative != SOURCE_ENGINE:
                            content = bytes(captured)
                inventory[relative] = ArchiveEntry(
                    kind=kind,
                    mode=member.mode & 0o7777,
                    linkname=member.linkname if member.issym() else None,
                    content=content,
                    sha256=digest,
                    preset_cohort_id=preset_cohort_id,
                )
    except (OSError, tarfile.TarError) as error:
        raise PackageAuditError(f"cannot read package archive: {error}") from error
    if package_root is None:
        raise PackageAuditError("package archive is empty")
    return inventory


def _require_kind(
    inventory: Mapping[str, ArchiveEntry], paths: set[str], kind: str
) -> None:
    for path in sorted(paths):
        entry = inventory.get(path)
        if entry is None:
            raise PackageAuditError(f"package is missing {path}")
        if entry.kind != kind:
            raise PackageAuditError(f"package member has wrong type: {path}")


def _require_executable(
    inventory: Mapping[str, ArchiveEntry], paths: set[str]
) -> None:
    _require_kind(inventory, paths, "file")
    for path in sorted(paths):
        if inventory[path].mode & 0o111 == 0:
            raise PackageAuditError(
                f"package executable has no execute bit: {path}"
            )


def _source_path_is_forbidden(path: str) -> bool:
    lower_path = path.lower()
    basename = pathlib.PurePosixPath(lower_path).name
    if lower_path in SOURCE_FORBIDDEN_FILES or lower_path.startswith(
        SOURCE_FORBIDDEN_PREFIXES
    ):
        return True
    if basename in SOURCE_FORBIDDEN_BASENAMES:
        return True
    if basename.endswith(SOURCE_PRIVATE_SUFFIXES):
        return True
    if "private" in basename and basename.endswith(
        SOURCE_PRIVATE_NAMED_SUFFIXES
    ):
        return True
    if "product" in basename and "pepper" in basename:
        return True
    if "model-provision" in basename or "provisioner" in basename:
        return True
    if "release" in basename and "bootstrap" in basename:
        return True
    if "private-key" in basename or "private_key" in basename:
        return True
    return False


def _production_path_is_forbidden(path: str) -> bool:
    lower_path = path.lower()
    basename = pathlib.PurePosixPath(lower_path).name
    if lower_path in PRODUCTION_FORBIDDEN_FILES:
        return True
    return basename in PRODUCTION_FORBIDDEN_MODEL_GUARD_BASENAMES


def source_build_identity(
    inventory: Mapping[str, ArchiveEntry],
) -> dict[str, str]:
    identity_entry = inventory.get(SOURCE_BUILD_IDENTITY)
    engine_entry = inventory.get(SOURCE_ENGINE)
    version_entry = inventory.get(SOURCE_VERSION)
    if (
        identity_entry is None
        or identity_entry.content is None
        or engine_entry is None
        or engine_entry.sha256 is None
        or version_entry is None
        or version_entry.content is None
    ):
        raise PackageAuditError(
            "SOURCE package identity inputs were not captured"
        )

    content = identity_entry.content
    if (
        not content
        or not content.endswith(b"\n")
        or b"\r" in content
        or b"\x00" in content
    ):
        raise PackageAuditError("SOURCE build identity encoding is invalid")
    try:
        lines = content.decode("ascii").removesuffix("\n").split("\n")
    except UnicodeDecodeError as error:
        raise PackageAuditError(
            "SOURCE build identity must be ASCII"
        ) from error
    values: dict[str, str] = {}
    keys: list[str] = []
    for line in lines:
        if line.count("=") != 1:
            raise PackageAuditError("SOURCE build identity line is invalid")
        key, value = line.split("=", 1)
        if not key or not value or key in values:
            raise PackageAuditError("SOURCE build identity line is invalid")
        keys.append(key)
        values[key] = value
    if tuple(keys) != SOURCE_BUILD_IDENTITY_KEYS:
        raise PackageAuditError(
            "SOURCE build identity keys or canonical order are invalid"
        )
    if values["format"] != SOURCE_BUILD_IDENTITY_FORMAT:
        raise PackageAuditError("SOURCE build identity format is incompatible")
    if re.fullmatch(r"[0-9a-f]{40}", values["edge_commit"]) is None:
        raise PackageAuditError("SOURCE Edge commit is malformed")
    if re.fullmatch(r"V[0-9]+(?:\.[0-9]+){2}", values["version"]) is None:
        raise PackageAuditError("SOURCE package version is malformed")
    for key in ("engine_sha256", "build_identity"):
        if re.fullmatch(r"[0-9a-f]{64}", values[key]) is None:
            raise PackageAuditError(f"SOURCE {key} is malformed")
    if values["engine_sha256"] != engine_entry.sha256:
        raise PackageAuditError("SOURCE engine SHA-256 differs from identity")
    if version_entry.content != f"{values['version']}\n".encode("ascii"):
        raise PackageAuditError("SOURCE version.txt differs from identity")
    hash_input = (
        f"{SOURCE_BUILD_IDENTITY_FORMAT}\n"
        f"edge_commit={values['edge_commit']}\n"
        f"version={values['version']}\n"
        f"engine_sha256={values['engine_sha256']}\n"
    ).encode("ascii")
    if hashlib.sha256(hash_input).hexdigest() != values["build_identity"]:
        raise PackageAuditError("SOURCE build identity digest is invalid")
    return values


def file_sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while True:
            block = source.read(4 * 1024 * 1024)
            if not block:
                break
            digest.update(block)
    return digest.hexdigest()


def verify_source_archive_name(
    archive: pathlib.Path, identity: Mapping[str, str], archive_sha256: str
) -> None:
    suffix = (
        f"-SOURCE-{identity['edge_commit']}-"
        f"{identity['build_identity']}-{archive_sha256}.tar.gz"
    )
    if not archive.name.endswith(suffix):
        raise PackageAuditError(
            "SOURCE archive name does not bind commit, build identity, "
            "and archive SHA-256"
        )


def verify_source_preset_cohort(
    inventory: Mapping[str, ArchiveEntry],
) -> None:
    cohorts: set[str] = set()
    for path, entry in sorted(inventory.items()):
        if not release_schema._is_preset_model_payload_path(path):
            continue
        if entry.kind != "file":
            raise PackageAuditError(
                f"SOURCE preset model must be a regular file: {path}"
            )
        cohort = entry.preset_cohort_id
        if cohort is None or re.fullmatch(r"[0-9a-f]{32}", cohort) is None:
            raise PackageAuditError(
                f"SOURCE preset model lacks strict CEM v2 validation: {path}"
            )
        if cohort == "0" * 32:
            raise PackageAuditError(
                f"SOURCE preset model cohort ID must be nonzero: {path}"
            )
        cohorts.add(cohort)
    if len(cohorts) > 1:
        raise PackageAuditError("SOURCE preset models use mixed cohort IDs")


def verify_inventory(
    inventory: Mapping[str, ArchiveEntry],
    build_profile: str,
    legacy_migration: bool = False,
) -> str:
    if build_profile not in PROFILES:
        raise PackageAuditError(f"unsupported build profile: {build_profile}")

    _require_kind(inventory, COMMON_DIRECTORIES, "directory")
    _require_kind(inventory, COMMON_FILES, "file")
    for path, target in COMMON_SYMLINKS.items():
        entry = inventory.get(path)
        if (
            entry is None
            or entry.kind != "symlink"
            or entry.linkname != target
        ):
            raise PackageAuditError(
                f"package symlink is missing or invalid: {path}"
            )

    if build_profile == PROFILE_SOURCE:
        _require_executable(inventory, SOURCE_EXECUTABLES)
        _require_kind(inventory, SOURCE_FILES, "file")
        source_build_identity(inventory)
        verify_source_preset_cohort(inventory)
        for path in sorted(inventory):
            if legacy_migration and path in {
                "scripts/install.sh",
                "scripts/start.sh",
                "scripts/inte_run_start.sh",
            }:
                continue
            if _source_path_is_forbidden(path):
                raise PackageAuditError(
                    f"SOURCE package contains controlled release material: {path}"
                )
        return "SOURCE"

    _require_executable(inventory, PRODUCTION_EXECUTABLES)
    _require_kind(inventory, PRODUCTION_FILES, "file")
    for path in sorted(inventory):
        if _production_path_is_forbidden(path):
            raise PackageAuditError(
                "production package contains SOURCE-only or device-specific "
                f"material: {path}"
            )
    return PROFILE_PRODUCTION


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--archive", required=True)
    parser.add_argument("--build-profile", choices=PROFILES, required=True)
    parser.add_argument("--legacy-migration", action="store_true")
    arguments = parser.parse_args()
    try:
        archive = pathlib.Path(arguments.archive)
        inventory = read_inventory(archive)
        variant = verify_inventory(
            inventory, arguments.build_profile, arguments.legacy_migration
        )
        archive_sha256 = file_sha256(archive)
        if arguments.build_profile == PROFILE_SOURCE:
            identity = source_build_identity(inventory)
            if not arguments.legacy_migration:
                verify_source_archive_name(archive, identity, archive_sha256)
    except PackageAuditError as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print(f"Verified package content profile: {variant}")
    if arguments.build_profile == PROFILE_SOURCE:
        print(f"Edge commit: {identity['edge_commit']}")
        print(f"Build identity: {identity['build_identity']}")
    print(f"Archive SHA-256: {archive_sha256}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
