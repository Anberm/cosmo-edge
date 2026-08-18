#!/usr/bin/python3
"""Audit permanent MD5 upgrade packages for the Open and Protected editions."""

from __future__ import annotations

import argparse
import hashlib
import pathlib
import re
import stat
import tarfile


class PackageAuditError(RuntimeError):
    pass


PROFILES = ("public-runtime", "production-release")
REQUIRED_DIRS = {"bin", "files", "font", "lib", "resource", "scripts", "web"}
REQUIRED_EXECUTABLES = {
    "bin/cosmo-engine",
    "scripts/install.sh",
    "scripts/inte_run_start.sh",
    "scripts/run_start.sh",
    "scripts/start.sh",
    "scripts/stop.sh",
}
REQUIRED_FILES = {"bin/version.txt", "scripts/common.sh"}
PRIVATE_MARKERS = (
    b"-----BEGIN PRIVATE KEY-----",
    b"-----BEGIN ENCRYPTED PRIVATE KEY-----",
    b"-----BEGIN RSA PRIVATE KEY-----",
    b"-----BEGIN EC PRIVATE KEY-----",
    b"-----BEGIN OPENSSH PRIVATE KEY-----",
)
FORBIDDEN_BASENAMES = {
    "commissioning-ed25519.seed",
    "device-certificate.bin",
    "product-model-key-v1.bin",
    "product-pepper-v1.bin",
    "release-private-key.o",
}


def archive_md5(path: pathlib.Path) -> str:
    digest = hashlib.md5(usedforsecurity=False)
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def verify_archive_name(path: pathlib.Path) -> None:
    match = re.fullmatch(
        r"cosmo-[Vv]\d+\.\d+\.\d+-([0-9a-fA-F]{32})\.tar\.gz", path.name
    )
    if match is None or match.group(1).lower() != archive_md5(path):
        raise PackageAuditError("archive name must contain its exact MD5 digest")


def verify_package(path: pathlib.Path, profile: str) -> None:
    if not path.is_absolute() or path.is_symlink() or not path.is_file():
        raise PackageAuditError("archive must be one absolute regular file")
    verify_archive_name(path)
    entries: dict[str, tarfile.TarInfo] = {}
    contents: dict[str, bytes] = {}
    root: str | None = None
    with tarfile.open(path, "r:gz") as archive:
        for member in archive:
            parts = pathlib.PurePosixPath(member.name).parts
            if not parts or any(part in ("", ".", "..") for part in parts):
                raise PackageAuditError(f"unsafe archive member: {member.name}")
            root = parts[0] if root is None else root
            if parts[0] != root:
                raise PackageAuditError("archive must contain exactly one package root")
            relative = pathlib.PurePosixPath(*parts[1:]).as_posix()
            if not relative or relative == ".":
                continue
            if relative in entries or not (member.isdir() or member.isreg() or member.issym()):
                raise PackageAuditError(f"unsupported or duplicate member: {relative}")
            entries[relative] = member
            if member.isreg():
                stream = archive.extractfile(member)
                if stream is None:
                    raise PackageAuditError(f"cannot read member: {relative}")
                data = stream.read()
                contents[relative] = data
                if any(marker in data for marker in PRIVATE_MARKERS):
                    raise PackageAuditError(f"private key material is forbidden: {relative}")

    for directory in REQUIRED_DIRS:
        if directory not in entries or not entries[directory].isdir():
            raise PackageAuditError(f"required directory is missing: {directory}")
    for filename in REQUIRED_FILES:
        if filename not in entries or not entries[filename].isreg():
            raise PackageAuditError(f"required file is missing: {filename}")
    for filename in REQUIRED_EXECUTABLES:
        entry = entries.get(filename)
        if entry is None or not entry.isreg() or not (entry.mode & stat.S_IXUSR):
            raise PackageAuditError(f"required executable is missing: {filename}")

    for filename in entries:
        if pathlib.PurePosixPath(filename).name.lower() in FORBIDDEN_BASENAMES:
            raise PackageAuditError(f"controlled secret is forbidden: {filename}")
        if filename.startswith(".release-bootstrap/") or "release_updater" in filename:
            raise PackageAuditError(f"obsolete signed-release material is forbidden: {filename}")

    provision = entries.get("bin/cosmo-model-provision")
    if profile == "public-runtime" and provision is not None:
        raise PackageAuditError("Open package must not contain the provisioning tool")
    if profile == "production-release" and (
        provision is None or not provision.isreg() or not (provision.mode & stat.S_IXUSR)
    ):
        raise PackageAuditError("Protected package requires cosmo-model-provision")

    models = {
        name: data
        for name, data in contents.items()
        if name.startswith("resource/models/") and name.endswith("/model.nn")
    }
    for name, data in models.items():
        encrypted = data.startswith(b"CEMC")
        if profile == "public-runtime" and encrypted:
            raise PackageAuditError(f"Open package contains an encrypted preset model: {name}")
        if profile == "production-release" and not encrypted:
            raise PackageAuditError(f"Protected package contains a plaintext preset model: {name}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--archive", required=True)
    parser.add_argument("--build-profile", required=True, choices=PROFILES)
    arguments = parser.parse_args()
    try:
        verify_package(pathlib.Path(arguments.archive), arguments.build_profile)
    except (OSError, tarfile.TarError, PackageAuditError) as error:
        parser.error(str(error))
    print(f"Verified {arguments.build_profile} MD5 upgrade package: {arguments.archive}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
