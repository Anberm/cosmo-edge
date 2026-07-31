#!/usr/bin/env python3
"""Fail-closed verifier for one Cosmo Model Guard v2 consumer SDK."""

from __future__ import annotations

import argparse
import hashlib
import importlib.machinery
import importlib.util
import os
import pathlib
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import unicodedata
from typing import Mapping, NoReturn


SCRIPT_DIRECTORY = pathlib.Path(__file__).resolve().parent
RELEASE_SCHEMA_SOURCE = SCRIPT_DIRECTORY / "release_updater.py"
release_schema_loader = importlib.machinery.SourceFileLoader(
    "cosmo_model_guard_sdk_release_schema", str(RELEASE_SCHEMA_SOURCE)
)
release_schema_spec = importlib.util.spec_from_loader(
    release_schema_loader.name, release_schema_loader
)
if release_schema_spec is None:
    raise RuntimeError("cannot load Model Guard ABI schema")
release_schema = importlib.util.module_from_spec(release_schema_spec)
sys.modules[release_schema_loader.name] = release_schema
release_schema_loader.exec_module(release_schema)


EXPECTED_V2_EXPORTS = {
    "CmgV2CloseArtifact@@CMG_2.0",
    "CmgV2GetArtifactInfo@@CMG_2.0",
    "CmgV2LoadSophonSegment@@CMG_2.0",
    "CmgV2OpenArtifact@@CMG_2.0",
}
EXPECTED_ALLOWLIST = {name.split("@@", 1)[0] for name in EXPECTED_V2_EXPORTS}
PROFILE_V2_ONLY = "v2-only"
ADMISSION_PUBLIC_RUNTIME = "public-runtime"
ADMISSION_PRODUCTION_RELEASE = "production-release"
ADMISSION_TEST_FIXTURE = "test-fixture"
ADMISSION_PROFILES = (
    ADMISSION_PUBLIC_RUNTIME,
    ADMISSION_PRODUCTION_RELEASE,
    ADMISSION_TEST_FIXTURE,
)
SHA256_PATTERN = re.compile(r"[0-9a-f]{64}\Z")
EXPECTED_NEEDED = {"libbmrt.so.1.0", "libcrypto.so.3"}
ALLOWED_NEEDED = EXPECTED_NEEDED | {
    "libstdc++.so.6",
    "libgcc_s.so.1",
    "libc.so.6",
    "ld-linux-aarch64.so.1",
}
EXPECTED_PROVISION_NEEDED = {
    "libcrypto.so.3",
    "libstdc++.so.6",
    "libgcc_s.so.1",
    "libc.so.6",
    "ld-linux-aarch64.so.1",
}
EXPECTED_PROVISION_RUNPATH = "$ORIGIN/../lib"
TEST_FIXTURE_MARKER_NAME = "TEST_FIXTURE_DO_NOT_DEPLOY"
TEST_FIXTURE_MARKER_CONTENT = b"COSMO_MODEL_GUARD_V2_TEST_FIXTURE_DO_NOT_DEPLOY\n"
SDK_RELEASE_MANIFEST_NAME = "sdk-release.env"
MAX_SDK_RELEASE_MANIFEST_BYTES = 16 * 1024
SDK_RELEASE_KEYS = (
    "CMG_SDK_ABI_SHA256",
    "CMG_SDK_DEPENDENCIES_SHA256",
    "CMG_SDK_HEADER_SHA256",
    "CMG_SDK_LIBRARY_SHA256",
    "CMG_SDK_RELEASE_FORMAT",
    "CMG_SDK_RELEASE_ID",
)
SDK_RELEASE_FIXED_VALUES = {
    "CMG_SDK_RELEASE_FORMAT": "cosmo-model-guard-sdk-release-v2",
    "CMG_SDK_RELEASE_ID": "cmg-sdk-v2.3.3",
}
SDK_RELEASE_SHA256_KEYS = {
    "CMG_SDK_ABI_SHA256",
    "CMG_SDK_DEPENDENCIES_SHA256",
    "CMG_SDK_HEADER_SHA256",
    "CMG_SDK_LIBRARY_SHA256",
}
SDK_RELEASE_COMPONENT_KEYS = {
    "CMG_SDK_ABI_SHA256": "abi",
    "CMG_SDK_DEPENDENCIES_SHA256": "dependencies",
    "CMG_SDK_HEADER_SHA256": "header",
    "CMG_SDK_LIBRARY_SHA256": "library",
}
DEPENDENCY_TREE_DOMAIN = b"cosmo-dependency-tree-v1\x00"
OPENSSL_HEADERS_PATH = "thirdparty/openssl/include"
OPENSSL_LIBCRYPTO_PATH = "thirdparty/openssl/lib/libcrypto.so.3"
SOPHON_HEADERS_PATH = "thirdparty/sophon/include"
SOPHON_LIBRARIES_PATH = "thirdparty/sophon/lib"
SOPHON_LIBRARY_PATHS = {
    "libbmrt_link": f"{SOPHON_LIBRARIES_PATH}/libbmrt.so",
    "libbmrt_runtime": f"{SOPHON_LIBRARIES_PATH}/libbmrt.so.1.0",
    "libbmlib_link": f"{SOPHON_LIBRARIES_PATH}/libbmlib.so",
    "libbmlib_runtime": f"{SOPHON_LIBRARIES_PATH}/libbmlib.so.0",
}
MAX_DEPENDENCY_HEADER_BYTES = 8 * 1024 * 1024
MAX_DEPENDENCY_HEADERS_BYTES = 128 * 1024 * 1024
MAX_DEPENDENCY_LIBRARY_BYTES = 512 * 1024 * 1024
MAX_DEPENDENCY_LIBRARIES_BYTES = 4 * 1024 * 1024 * 1024


def fail(message: str) -> NoReturn:
    raise RuntimeError(message)


def require_isolated_entrypoint() -> None:
    if sys.flags.isolated != 1:
        fail(
            "Model Guard SDK verification must be launched with "
            "/usr/bin/python3 -I -B"
        )


def checked_directory(path: pathlib.Path) -> None:
    if not path.is_dir():
        fail(f"SDK directory is missing: {path}")


def metadata_snapshot(metadata: os.stat_result) -> tuple[int, ...]:
    return (
        metadata.st_dev,
        metadata.st_ino,
        metadata.st_size,
        metadata.st_mtime_ns,
    )


def checked_file(
    path: pathlib.Path,
    maximum_size: int,
    *,
    allow_empty: bool = False,
) -> bytes:
    metadata = path.stat()
    if (
        not stat.S_ISREG(metadata.st_mode)
        or (not allow_empty and metadata.st_size == 0)
        or metadata.st_size > maximum_size
    ):
        fail(f"SDK file is not a bounded regular file: {path}")

    flags = os.O_RDONLY | os.O_CLOEXEC
    descriptor = os.open(path, flags)
    try:
        before = os.fstat(descriptor)
        if metadata_snapshot(before) != metadata_snapshot(metadata):
            fail(f"SDK file changed before it was opened: {path}")
        data = bytearray()
        while len(data) < before.st_size:
            block = os.read(descriptor, min(1024 * 1024, before.st_size - len(data)))
            if not block:
                fail(f"SDK file was truncated while being read: {path}")
            data.extend(block)
        if os.read(descriptor, 1):
            fail(f"SDK file grew while being read: {path}")
        after = os.fstat(descriptor)
    finally:
        os.close(descriptor)
    current = path.stat()
    if (
        metadata_snapshot(after) != metadata_snapshot(before)
        or metadata_snapshot(current) != metadata_snapshot(before)
    ):
        fail(f"SDK file changed while being read: {path}")
    return bytes(data)


def checked_symlink(path: pathlib.Path, expected_target: str) -> None:
    before = path.lstat()
    if not stat.S_ISLNK(before.st_mode) or os.readlink(path) != expected_target:
        fail(f"SDK linker alias is invalid: {path}")
    after = path.lstat()
    if metadata_snapshot(after) != metadata_snapshot(before):
        fail(f"SDK linker alias changed while being read: {path}")


def _checked_tree_inventory(
    root: pathlib.Path,
) -> tuple[
    dict[str, tuple[tuple[int, ...], tuple[tuple[str, str], ...]]],
    tuple[str, ...],
]:
    root_metadata = root.stat()
    if not stat.S_ISDIR(root_metadata.st_mode):
        fail(f"dependency tree root is not a directory: {root}")

    directories: dict[str, tuple[tuple[int, ...], tuple[tuple[str, str], ...]]] = {}
    files: list[str] = []

    def visit(
        directory: pathlib.Path,
        relative_directory: pathlib.PurePosixPath,
        ancestors: frozenset[tuple[int, int]],
    ) -> None:
        before = directory.stat()
        if not stat.S_ISDIR(before.st_mode):
            fail(f"dependency tree contains a non-directory component: {directory}")
        identity = (before.st_dev, before.st_ino)
        if identity in ancestors:
            fail(f"dependency tree contains a directory cycle: {directory}")
        child_ancestors = ancestors | {identity}
        entries: list[tuple[str, str]] = []
        with os.scandir(directory) as iterator:
            try:
                scanned = sorted(
                    iterator, key=lambda item: item.name.encode("utf-8")
                )
            except UnicodeEncodeError:
                fail(f"dependency tree entry name is not UTF-8: {directory}")
        for entry in scanned:
            normalized_name = unicodedata.normalize("NFC", entry.name)
            if (
                normalized_name != entry.name
                or normalized_name in {"", ".", ".."}
                or "/" in normalized_name
                or "\x00" in normalized_name
            ):
                fail(
                    "dependency tree entry name is not canonical UTF-8 NFC: "
                    f"{entry.path}"
                )
            entry_path = directory / entry.name
            entry_metadata = entry_path.stat()
            relative = relative_directory / normalized_name
            relative_text = relative.as_posix()
            if stat.S_ISDIR(entry_metadata.st_mode):
                entries.append((normalized_name, "directory"))
                visit(entry_path, relative, child_ancestors)
            elif stat.S_ISREG(entry_metadata.st_mode):
                entries.append((normalized_name, "file"))
                files.append(relative_text)
            else:
                fail(f"dependency tree special file is forbidden: {entry_path}")
        after = directory.stat()
        if metadata_snapshot(after) != metadata_snapshot(before):
            fail(
                "dependency tree directory changed while being inventoried: "
                f"{directory}"
            )
        directories[relative_directory.as_posix()] = (
            metadata_snapshot(before),
            tuple(entries),
        )

    visit(root, pathlib.PurePosixPath("."), frozenset())
    return directories, tuple(files)


def capture_dependency_tree(
    root: pathlib.Path,
    *,
    maximum_file_size: int,
    maximum_total_size: int,
) -> dict[str, bytes]:
    if not root.is_absolute():
        fail(f"dependency tree root must be absolute: {root}")
    before_directories, before_files = _checked_tree_inventory(root)
    captured: dict[str, bytes] = {}
    total = 0
    for relative in before_files:
        data = checked_file(
            root / pathlib.PurePosixPath(relative),
            maximum_file_size,
            allow_empty=True,
        )
        total += len(data)
        if total > maximum_total_size:
            fail(f"dependency tree is too large: {root}")
        captured[relative] = data
    after_directories, after_files = _checked_tree_inventory(root)
    if (
        after_directories != before_directories
        or after_files != before_files
        or set(captured) != set(before_files)
    ):
        fail(f"dependency tree inventory changed while being captured: {root}")
    return captured


def dependency_tree_digest(files: Mapping[str, bytes]) -> str:
    digest = hashlib.sha256(DEPENDENCY_TREE_DOMAIN)
    ordered: list[tuple[bytes, bytes]] = []
    for relative, data in files.items():
        normalized = unicodedata.normalize(
            "NFC", pathlib.PurePosixPath(relative).as_posix()
        )
        if normalized != relative or normalized in {"", "."}:
            fail(f"dependency tree relative path is not canonical: {relative}")
        ordered.append((normalized.encode("utf-8"), data))
    for encoded_path, data in sorted(ordered, key=lambda item: item[0]):
        digest.update(len(encoded_path).to_bytes(8, "big"))
        digest.update(encoded_path)
        digest.update(len(data).to_bytes(8, "big"))
        digest.update(data)
    return digest.hexdigest()


def _snapshot_digest(
    files: Mapping[str, tuple[bytes, int]], symlinks: Mapping[str, str]
) -> str:
    digest = hashlib.sha256(b"cosmo-model-guard-sdk-snapshot-v1\x00")
    for relative, (data, mode) in sorted(files.items()):
        encoded = relative.encode("utf-8")
        digest.update(b"F")
        digest.update(len(encoded).to_bytes(4, "big"))
        digest.update(encoded)
        digest.update(mode.to_bytes(2, "big"))
        digest.update(len(data).to_bytes(8, "big"))
        digest.update(data)
    for relative, target in sorted(symlinks.items()):
        encoded = relative.encode("utf-8")
        encoded_target = target.encode("utf-8")
        digest.update(b"L")
        digest.update(len(encoded).to_bytes(4, "big"))
        digest.update(encoded)
        digest.update(len(encoded_target).to_bytes(4, "big"))
        digest.update(encoded_target)
    return digest.hexdigest()


def _write_snapshot_file(path: pathlib.Path, data: bytes, mode: int) -> None:
    descriptor = os.open(
        path,
        os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC,
        0o600,
    )
    try:
        offset = 0
        while offset < len(data):
            offset += os.write(descriptor, data[offset:])
        os.fdatasync(descriptor)
    finally:
        os.close(descriptor)
    os.chmod(path, mode)


def _expected_snapshot_directories(paths: set[str]) -> set[str]:
    directories: set[str] = set()
    for relative in paths:
        parent = pathlib.PurePosixPath(relative).parent
        while parent != pathlib.PurePosixPath("."):
            directories.add(parent.as_posix())
            parent = parent.parent
    return directories


def _validate_input_snapshot(
    root: pathlib.Path,
    files: Mapping[str, tuple[bytes, int]],
    symlinks: Mapping[str, str],
) -> None:
    if not root.is_dir():
        fail("verified SDK snapshot root is missing")
    expected_directories = _expected_snapshot_directories(
        set(files) | set(symlinks)
    )
    observed: set[str] = set()
    for current_root, directory_names, file_names in os.walk(
        root, topdown=True, followlinks=False
    ):
        current = pathlib.Path(current_root)
        for name in directory_names + file_names:
            observed.add((current / name).relative_to(root).as_posix())
    if observed != expected_directories | set(files) | set(symlinks):
        fail("verified SDK snapshot contains unexpected or missing entries")
    for relative in expected_directories:
        if not (root / relative).is_dir():
            fail(f"verified SDK snapshot directory is missing: {relative}")
    for relative, (expected_data, _mode) in files.items():
        actual = checked_file(
            root / relative,
            max(len(expected_data), 1),
            allow_empty=True,
        )
        if actual != expected_data:
            fail(f"verified SDK snapshot file differs from source: {relative}")
    for relative, target in symlinks.items():
        checked_symlink(root / relative, target)


def stage_input_snapshot(
    snapshot_base: pathlib.Path,
    files: Mapping[str, tuple[bytes, int]],
    symlinks: Mapping[str, str],
    expected_root: pathlib.Path | None = None,
) -> pathlib.Path:
    if not snapshot_base.is_absolute():
        fail("verified SDK snapshot base must be absolute")
    try:
        snapshot_base.mkdir(mode=0o700, parents=True)
    except FileExistsError:
        pass
    if not snapshot_base.is_dir():
        fail("verified SDK snapshot base is not a directory")
    digest = _snapshot_digest(files, symlinks)
    final = snapshot_base / digest
    if expected_root is not None:
        if not expected_root.is_absolute() or expected_root != final:
            fail("SDK inputs do not match the expected snapshot root")
    if not final.exists():
        candidate = pathlib.Path(
            tempfile.mkdtemp(prefix=".candidate-", dir=snapshot_base)
        )
        try:
            directories = _expected_snapshot_directories(
                set(files) | set(symlinks)
            )
            for relative in sorted(
                directories, key=lambda item: (item.count("/"), item)
            ):
                (candidate / relative).mkdir(mode=0o700)
            for relative, (data, mode) in files.items():
                _write_snapshot_file(candidate / relative, data, mode)
            for relative, target in symlinks.items():
                os.symlink(target, candidate / relative)
            try:
                os.rename(candidate, final)
            except FileExistsError:
                shutil.rmtree(candidate)
        except BaseException:
            if candidate.exists():
                shutil.rmtree(candidate)
            raise
    _validate_input_snapshot(final, files, symlinks)
    return final


def run_tool(tool: pathlib.Path, arguments: list[str]) -> str:
    if not tool.is_absolute() or not tool.is_file():
        fail(f"inspection tool is missing: {tool}")
    environment = dict(os.environ)
    environment["LC_ALL"] = "C"
    completed = subprocess.run(
        [str(tool), *arguments],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        env=environment,
    )
    if completed.returncode != 0:
        fail(f"inspection tool failed: {tool.name}")
    return completed.stdout


def verify_manifest(manifest_bytes: bytes, expected_profile: str) -> None:
    release_schema._validate_model_guard_abi_manifest(manifest_bytes, expected_profile)


def verify_header(header_bytes: bytes) -> None:
    release_schema._validate_model_guard_header(header_bytes)


def parse_sdk_release_manifest(
    manifest: bytes,
) -> dict[str, str]:
    if (
        not manifest
        or len(manifest) > MAX_SDK_RELEASE_MANIFEST_BYTES
        or not manifest.endswith(b"\n")
        or b"\r" in manifest
        or b"\x00" in manifest
    ):
        fail("Model Guard SDK release manifest encoding is invalid")
    try:
        text = manifest.decode("ascii")
    except UnicodeDecodeError:
        fail("Model Guard SDK release manifest must be ASCII")

    lines = text[:-1].split("\n")
    keys: list[str] = []
    values: dict[str, str] = {}
    for line in lines:
        if (
            line.count("=") != 1
            or re.fullmatch(r"CMG_SDK_[A-Z0-9_]+=[!-~]+", line) is None
        ):
            fail("Model Guard SDK release manifest line is invalid")
        key, value = line.split("=", 1)
        if key in values:
            fail("Model Guard SDK release manifest contains a duplicate key")
        keys.append(key)
        values[key] = value
    if tuple(keys) != SDK_RELEASE_KEYS:
        fail(
            "Model Guard SDK release manifest keys or canonical order are invalid"
        )

    for key, expected in SDK_RELEASE_FIXED_VALUES.items():
        if values[key] != expected:
            fail(f"Model Guard SDK release manifest {key} is incompatible")
    for key in SDK_RELEASE_SHA256_KEYS:
        if SHA256_PATTERN.fullmatch(values[key]) is None:
            fail(f"Model Guard SDK release manifest {key} is malformed")
    return values


def verify_sdk_release_manifest(
    manifest: bytes,
    *,
    library: bytes,
    header: bytes,
    abi_manifest: bytes,
    dependency_manifest: bytes,
    expected_profile: str,
) -> dict[str, str]:
    values = parse_sdk_release_manifest(manifest)
    if expected_profile != PROFILE_V2_ONLY:
        fail("cmg-sdk-v2.3.3 admits only the v2-only Guard profile")
    components: dict[str, bytes] = {
        "abi": abi_manifest,
        "dependencies": dependency_manifest,
        "header": header,
        "library": library,
    }
    for key, component_name in SDK_RELEASE_COMPONENT_KEYS.items():
        component = components[component_name]
        if hashlib.sha256(component).hexdigest() != values[key]:
            fail(
                "Model Guard SDK release manifest component SHA-256 mismatch: "
                f"{component_name}"
            )
    return values


def verify_dependencies(
    dependency_manifest: bytes,
    header: bytes,
    abi_manifest: bytes,
    expected_profile: str,
    snapshot_root: pathlib.Path,
    readelf: pathlib.Path,
) -> None:
    dependencies = release_schema._validate_model_guard_dependencies(
        dependency_manifest, header, abi_manifest, expected_profile
    )
    openssl = dependencies["openssl"]
    sophon = dependencies["sophon"]
    openssl_headers = capture_dependency_tree(
        snapshot_root / OPENSSL_HEADERS_PATH,
        maximum_file_size=MAX_DEPENDENCY_HEADER_BYTES,
        maximum_total_size=MAX_DEPENDENCY_HEADERS_BYTES,
    )
    sophon_headers = capture_dependency_tree(
        snapshot_root / SOPHON_HEADERS_PATH,
        maximum_file_size=MAX_DEPENDENCY_HEADER_BYTES,
        maximum_total_size=MAX_DEPENDENCY_HEADERS_BYTES,
    )
    sophon_libraries = capture_dependency_tree(
        snapshot_root / SOPHON_LIBRARIES_PATH,
        maximum_file_size=MAX_DEPENDENCY_LIBRARY_BYTES,
        maximum_total_size=MAX_DEPENDENCY_LIBRARIES_BYTES,
    )
    openssl_version_header = openssl_headers.get("openssl/opensslv.h")
    if openssl_version_header is None:
        fail("dependency snapshot omits a required version header")
    libcrypto_path = snapshot_root / OPENSSL_LIBCRYPTO_PATH
    libcrypto = checked_file(libcrypto_path, MAX_DEPENDENCY_LIBRARY_BYTES)
    openssl_header_version = release_schema._openssl_version_from_header(
        openssl_version_header
    )
    openssl_library_version = release_schema._openssl_version_from_library(
        libcrypto
    )
    if (
        openssl["headers_path"] != OPENSSL_HEADERS_PATH
        or openssl["headers_file_count"] != len(openssl_headers)
        or openssl["headers_sha256"] != dependency_tree_digest(openssl_headers)
        or openssl["version"] != openssl_header_version
        or openssl["version"] != openssl_library_version
        or openssl["version_header_sha256"]
        != hashlib.sha256(openssl_version_header).hexdigest()
        or openssl["libcrypto_path"] != OPENSSL_LIBCRYPTO_PATH
        or openssl["libcrypto_sha256"] != hashlib.sha256(libcrypto).hexdigest()
    ):
        fail("Model Guard OpenSSL provenance differs from the Edge build inputs")
    elf_header = run_tool(readelf, ["-hW", str(libcrypto_path)])
    dynamic = run_tool(readelf, ["-dW", str(libcrypto_path)])
    if (
        re.search(r"^\s*Machine:\s+AArch64\s*$", elf_header, re.MULTILINE)
        is None
        or re.findall(r"\(SONAME\).*\[([^]]+)\]", dynamic)
        != ["libcrypto.so.3"]
        or openssl["libcrypto_soname"] != "libcrypto.so.3"
    ):
        fail(
            "Edge libcrypto ELF identity differs from the dependency manifest"
        )
    if (
        sophon["headers_path"] != SOPHON_HEADERS_PATH
        or sophon["headers_file_count"] != len(sophon_headers)
        or sophon["headers_sha256"] != dependency_tree_digest(sophon_headers)
        or sophon["libraries_path"] != SOPHON_LIBRARIES_PATH
        or sophon["libraries_file_count"] != len(sophon_libraries)
        or sophon["libraries_sha256"] != dependency_tree_digest(sophon_libraries)
    ):
        fail("Model Guard Sophon provenance differs from the Edge build inputs")
    for field, snapshot_path in SOPHON_LIBRARY_PATHS.items():
        relative = pathlib.PurePosixPath(snapshot_path).relative_to(
            SOPHON_LIBRARIES_PATH
        ).as_posix()
        library = sophon_libraries.get(relative)
        if (
            library is None
            or sophon[f"{field}_path"] != snapshot_path
            or sophon[f"{field}_sha256"] != hashlib.sha256(library).hexdigest()
        ):
            fail("Model Guard Sophon provenance differs from the Edge build inputs")


def verify_elf(
    library: pathlib.Path,
    readelf: pathlib.Path,
    nm: pathlib.Path,
    expected_profile: str,
) -> None:
    header = run_tool(readelf, ["-h", str(library)])
    if re.search(r"^\s*Machine:\s+AArch64\s*$", header, re.MULTILINE) is None:
        fail("model guard SDK library is not AArch64")

    dynamic = run_tool(readelf, ["-d", str(library)])
    sonames = re.findall(r"\(SONAME\).*\[([^]]+)\]", dynamic)
    runpaths = re.findall(r"\(RUNPATH\).*\[([^]]+)\]", dynamic)
    needed = set(re.findall(r"\(NEEDED\).*\[([^]]+)\]", dynamic))
    if sonames != ["libcosmo_model_guard.so.2"]:
        fail("model guard SDK SONAME is not exactly major 2")
    if runpaths != ["$ORIGIN"]:
        fail("model guard SDK RUNPATH must be exactly $ORIGIN")
    if not EXPECTED_NEEDED.issubset(needed) or not needed.issubset(ALLOWED_NEEDED):
        fail(f"model guard SDK NEEDED set is incompatible: {sorted(needed)}")

    symbols = run_tool(nm, ["-D", "--defined-only", str(library)])
    exports: set[str] = set()
    for line in symbols.splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[1] in {"T", "W"}:
            exports.add(fields[2])
    if exports != EXPECTED_V2_EXPORTS:
        fail(f"model guard SDK exports are incompatible: {sorted(exports)}")


def verify_provision_tool(tool: pathlib.Path, readelf: pathlib.Path) -> None:
    header = run_tool(readelf, ["-h", str(tool)])
    if re.search(r"^\s*Machine:\s+AArch64\s*$", header, re.MULTILINE) is None:
        fail("cosmo-model-provision is not AArch64")
    if re.search(r"^\s*Type:\s+(?:DYN|EXEC)\b", header, re.MULTILINE) is None:
        fail("cosmo-model-provision is not an ELF executable")

    dynamic = run_tool(readelf, ["-d", str(tool)])
    needed = set(re.findall(r"\(NEEDED\).*\[([^]]+)\]", dynamic))
    runpaths = re.findall(r"\(RUNPATH\).*\[([^]]+)\]", dynamic)
    if needed != EXPECTED_PROVISION_NEEDED:
        fail(f"cosmo-model-provision NEEDED set is incompatible: {sorted(needed)}")
    if runpaths != [EXPECTED_PROVISION_RUNPATH] or "(RPATH)" in dynamic:
        fail(
            "cosmo-model-provision RUNPATH must be exactly "
            f"{EXPECTED_PROVISION_RUNPATH}"
        )
    if "(SONAME)" in dynamic:
        fail("cosmo-model-provision must not carry a shared-library SONAME")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--admission-profile",
        choices=ADMISSION_PROFILES,
        required=True,
    )
    parser.add_argument("--sdk-root", required=True)
    parser.add_argument("--snapshot-base", required=True)
    parser.add_argument("--expected-snapshot-root")
    parser.add_argument("--readelf", required=True)
    parser.add_argument("--nm", required=True)
    parser.add_argument("--openssl-include-dir", required=True)
    parser.add_argument("--libcrypto", required=True)
    parser.add_argument("--sophon-include-dir", required=True)
    parser.add_argument("--sophon-library-dir", required=True)
    parser.add_argument(
        "--expected-profile",
        choices=(PROFILE_V2_ONLY,),
        default=PROFILE_V2_ONLY,
    )
    arguments = parser.parse_args()
    public_runtime = arguments.admission_profile == ADMISSION_PUBLIC_RUNTIME
    test_fixture = arguments.admission_profile == ADMISSION_TEST_FIXTURE

    root = pathlib.Path(arguments.sdk_root)
    if not root.is_absolute():
        fail("SDK root must be absolute")
    include_directory = root / "include"
    library_directory = root / "lib"
    binary_directory = root / "bin"
    share_root = root / "share"
    share_directory = share_root / "cosmo-model-guard"
    required_directories = [
        root,
        include_directory,
        library_directory,
        share_root,
        share_directory,
    ]
    if not public_runtime:
        required_directories.append(binary_directory)
    for directory in required_directories:
        checked_directory(directory)

    header_path = include_directory / "cosmo_model_guard_v2.h"
    library_path = library_directory / "libcosmo_model_guard.so.2.0.0"
    manifest_path = share_directory / "cmg_v2_abi.json"
    dependency_manifest_path = share_directory / "cmg_v2_dependencies.json"
    sdk_release_manifest_path = share_directory / SDK_RELEASE_MANIFEST_NAME
    test_fixture_marker_path = share_directory / TEST_FIXTURE_MARKER_NAME
    header = checked_file(header_path, 128 * 1024)
    library = checked_file(library_path, 32 * 1024 * 1024)
    manifest = checked_file(manifest_path, 256 * 1024)
    dependency_manifest = checked_file(dependency_manifest_path, 64 * 1024)
    sdk_release_manifest: bytes | None = None
    try:
        sdk_release_manifest_path.lstat()
    except FileNotFoundError:
        if not test_fixture:
            raise
    else:
        sdk_release_manifest = checked_file(
            sdk_release_manifest_path,
            MAX_SDK_RELEASE_MANIFEST_BYTES,
        )
    provision_tool_path: pathlib.Path | None = None
    provision_tool: bytes | None = None
    if not public_runtime:
        provision_tool_path = binary_directory / "cosmo-model-provision"
        provision_tool = checked_file(
            provision_tool_path,
            32 * 1024 * 1024,
        )
    checked_symlink(library_directory / "libcosmo_model_guard.so.2", library_path.name)
    checked_symlink(library_directory / "libcosmo_model_guard.so", "libcosmo_model_guard.so.2")

    test_fixture_marker: bytes | None = None
    try:
        test_fixture_marker_path.lstat()
    except FileNotFoundError:
        pass
    else:
        test_fixture_marker = checked_file(
            test_fixture_marker_path,
            len(TEST_FIXTURE_MARKER_CONTENT),
        )
        if test_fixture_marker != TEST_FIXTURE_MARKER_CONTENT:
            fail("Model Guard SDK test-fixture marker content is invalid")
    if test_fixture and test_fixture_marker is None:
        fail("test-fixture admission requires the exact non-production SDK marker")
    if not test_fixture and test_fixture_marker is not None:
        fail(
            "Model Guard test fixtures are forbidden for public-runtime and "
            "production-release admission"
        )

    dependency_input_paths = {
        "OpenSSL include directory": pathlib.Path(arguments.openssl_include_dir),
        "libcrypto": pathlib.Path(arguments.libcrypto),
        "Sophon include directory": pathlib.Path(arguments.sophon_include_dir),
        "Sophon library directory": pathlib.Path(arguments.sophon_library_dir),
    }
    for description, path in dependency_input_paths.items():
        if not path.is_absolute():
            fail(f"{description} path must be absolute")
    if dependency_input_paths["libcrypto"].name != "libcrypto.so.3":
        fail("libcrypto input filename must be exactly libcrypto.so.3")
    openssl_headers = capture_dependency_tree(
        dependency_input_paths["OpenSSL include directory"],
        maximum_file_size=MAX_DEPENDENCY_HEADER_BYTES,
        maximum_total_size=MAX_DEPENDENCY_HEADERS_BYTES,
    )
    libcrypto = checked_file(
        dependency_input_paths["libcrypto"], MAX_DEPENDENCY_LIBRARY_BYTES
    )
    sophon_headers = capture_dependency_tree(
        dependency_input_paths["Sophon include directory"],
        maximum_file_size=MAX_DEPENDENCY_HEADER_BYTES,
        maximum_total_size=MAX_DEPENDENCY_HEADERS_BYTES,
    )
    sophon_libraries = capture_dependency_tree(
        dependency_input_paths["Sophon library directory"],
        maximum_file_size=MAX_DEPENDENCY_LIBRARY_BYTES,
        maximum_total_size=MAX_DEPENDENCY_LIBRARIES_BYTES,
    )

    snapshot_files: dict[str, tuple[bytes, int]] = {
        "include/cosmo_model_guard_v2.h": (header, 0o644),
        "lib/libcosmo_model_guard.so.2.0.0": (library, 0o644),
        "share/cosmo-model-guard/cmg_v2_abi.json": (manifest, 0o644),
        "share/cosmo-model-guard/cmg_v2_dependencies.json": (
            dependency_manifest,
            0o644,
        ),
    }
    if sdk_release_manifest is not None:
        snapshot_files[
            f"share/cosmo-model-guard/{SDK_RELEASE_MANIFEST_NAME}"
        ] = (sdk_release_manifest, 0o644)
    if provision_tool is not None:
        snapshot_files["bin/cosmo-model-provision"] = (provision_tool, 0o755)
    snapshot_symlinks = {
        "lib/libcosmo_model_guard.so.2": "libcosmo_model_guard.so.2.0.0",
        "lib/libcosmo_model_guard.so": "libcosmo_model_guard.so.2",
    }
    if test_fixture:
        assert test_fixture_marker is not None
        snapshot_files[
            f"share/cosmo-model-guard/{TEST_FIXTURE_MARKER_NAME}"
        ] = (test_fixture_marker, 0o644)
    for relative, data in openssl_headers.items():
        snapshot_files[f"{OPENSSL_HEADERS_PATH}/{relative}"] = (data, 0o600)
    snapshot_files[OPENSSL_LIBCRYPTO_PATH] = (libcrypto, 0o600)
    for relative, data in sophon_headers.items():
        snapshot_files[f"{SOPHON_HEADERS_PATH}/{relative}"] = (data, 0o600)
    for relative, data in sophon_libraries.items():
        snapshot_files[f"{SOPHON_LIBRARIES_PATH}/{relative}"] = (data, 0o600)

    snapshot_root = stage_input_snapshot(
        pathlib.Path(arguments.snapshot_base),
        snapshot_files,
        snapshot_symlinks,
        (
            pathlib.Path(arguments.expected_snapshot_root)
            if arguments.expected_snapshot_root is not None
            else None
        ),
    )
    header = checked_file(
        snapshot_root / "include/cosmo_model_guard_v2.h", 128 * 1024
    )
    library_path = snapshot_root / "lib/libcosmo_model_guard.so.2.0.0"
    library = checked_file(library_path, 32 * 1024 * 1024)
    if provision_tool is not None:
        provision_tool_path = snapshot_root / "bin/cosmo-model-provision"
        provision_tool = checked_file(
            provision_tool_path, 32 * 1024 * 1024
        )
    manifest = checked_file(
        snapshot_root / "share/cosmo-model-guard/cmg_v2_abi.json",
        256 * 1024,
    )
    dependency_manifest = checked_file(
        snapshot_root / "share/cosmo-model-guard/cmg_v2_dependencies.json",
        64 * 1024,
    )
    if sdk_release_manifest is not None:
        sdk_release_manifest = checked_file(
            snapshot_root
            / f"share/cosmo-model-guard/{SDK_RELEASE_MANIFEST_NAME}",
            MAX_SDK_RELEASE_MANIFEST_BYTES,
        )

    verify_header(header)
    verify_manifest(
        manifest,
        arguments.expected_profile,
    )
    verify_dependencies(
        dependency_manifest,
        header,
        manifest,
        arguments.expected_profile,
        snapshot_root,
        pathlib.Path(arguments.readelf),
    )
    sdk_release_values: dict[str, str] | None = None
    if sdk_release_manifest is not None:
        sdk_release_values = verify_sdk_release_manifest(
            sdk_release_manifest,
            library=library,
            header=header,
            abi_manifest=manifest,
            dependency_manifest=dependency_manifest,
            expected_profile=arguments.expected_profile,
        )
    verify_elf(
        library_path,
        pathlib.Path(arguments.readelf),
        pathlib.Path(arguments.nm),
        arguments.expected_profile,
    )
    if provision_tool_path is not None:
        verify_provision_tool(provision_tool_path, pathlib.Path(arguments.readelf))
    print(f"admission_profile={arguments.admission_profile}")
    print(f"verified_sdk_root={snapshot_root}")
    print(f"header_sha256={hashlib.sha256(header).hexdigest()}")
    print(f"library_sha256={hashlib.sha256(library).hexdigest()}")
    print(f"abi_manifest_sha256={hashlib.sha256(manifest).hexdigest()}")
    print(
        "dependency_manifest_sha256="
        f"{hashlib.sha256(dependency_manifest).hexdigest()}"
    )
    if sdk_release_manifest is not None:
        assert sdk_release_values is not None
        print(
            "sdk_release_manifest_sha256="
            f"{hashlib.sha256(sdk_release_manifest).hexdigest()}"
        )
        print(f"sdk_release_id={sdk_release_values['CMG_SDK_RELEASE_ID']}")
    else:
        assert test_fixture
        print("sdk_release_id=not-applicable-test-fixture")
    if provision_tool is not None:
        print(f"provision_tool_sha256={hashlib.sha256(provision_tool).hexdigest()}")
    if public_runtime:
        print("sdk_profile=public-runtime")
    elif test_fixture:
        assert test_fixture_marker is not None
        print("sdk_profile=TEST-FIXTURE-DO-NOT-DEPLOY")
        print(
            "test_fixture_marker_sha256="
            f"{hashlib.sha256(test_fixture_marker).hexdigest()}"
        )
    else:
        print("sdk_profile=production")
    return 0


if __name__ == "__main__":
    try:
        require_isolated_entrypoint()
        sys.exit(main())
    except (OSError, RuntimeError) as error:
        print(f"model guard v2 SDK verification failed: {error}", file=sys.stderr)
        sys.exit(1)
