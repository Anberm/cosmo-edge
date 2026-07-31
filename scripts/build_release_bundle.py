#!/usr/bin/python3 -I
"""Create a signed, deterministic Cosmo release bundle (offline only).

The Ed25519 private key is accepted only on inherited file descriptor 3.  The
descriptor must be a pipe or an anonymous ``memfd`` carrying every write,
grow, shrink, and seal seal; command-line paths, environment variables, and
ordinary key files are deliberately unsupported.

This tool is an offline release step.  The ordinary Docker package build emits
the installable SOURCE package, not a signed production release.  Only this
separate controlled ceremony creates a signed release archive.
"""

from __future__ import annotations

import argparse
import base64
import contextlib
import ctypes
import dataclasses
import errno
import fcntl
import gzip
import hashlib
import hmac
import importlib.machinery
import importlib.util
import io
import json
import os
import re
import stat
import struct
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path
from typing import Any, Mapping, Sequence


sys.dont_write_bytecode = True
SCRIPT_DIR = Path(__file__).resolve().parent
UPDATER_PATH = SCRIPT_DIR / "release_updater.py"
loader = importlib.machinery.SourceFileLoader("cosmo_release_updater", str(UPDATER_PATH))
spec = importlib.util.spec_from_loader(loader.name, loader)
if spec is None:
    raise RuntimeError("cannot load release updater schema")
schema = importlib.util.module_from_spec(spec)
sys.modules[loader.name] = schema
loader.exec_module(schema)


class PackagerError(RuntimeError):
    pass


BOOTSTRAP_TRUST_SYMBOL_SIZES = {
    "cosmo_release_public_key_raw_v1": 32,
    "cosmo_release_public_key_id_v1": 16,
    "cosmo_release_public_key_pem_sha256_v1": 32,
}
MODEL_GUARD_TRUST_SYMBOL_SIZES = {
    "cmg_product_pepper_bundle_v1": 64,
    "cmg_commissioning_public_key_bundle_v1": 64,
}
MODEL_GUARD_TRUST_SECTIONS = {
    "product_pepper_bundle": (".cmg.trust.product.v1", 64),
    "commissioning_public_key_bundle": (".cmg.trust.commissioning.v1", 64),
}
TEST_FIXTURE_MARKER_NAME = "TEST_FIXTURE_DO_NOT_DEPLOY"
TEST_FIXTURE_MARKER_CONTENT = b"COSMO_MODEL_GUARD_V2_TEST_FIXTURE_DO_NOT_DEPLOY\n"
BOOTSTRAP_REQUIRED_NEEDED = {"libcrypto.so.3"}
BOOTSTRAP_ALLOWED_NEEDED = BOOTSTRAP_REQUIRED_NEEDED | {
    "libstdc++.so.6",
    "libgcc_s.so.1",
    "libc.so.6",
    "ld-linux-aarch64.so.1",
}
EXPECTED_BOOTSTRAP_RUNPATH = "$ORIGIN/../lib"
MAX_BOOTSTRAP_ELF_BYTES = 64 * 1024 * 1024
MAX_MODEL_GUARD_ELF_BYTES = 64 * 1024 * 1024


def _fail(message: str) -> "NoReturn":
    raise PackagerError(message)


def _require_isolated_entrypoint() -> None:
    """Reject accidental unsafe launch modes before entering the ceremony.

    This check only detects misuse after Python has started.  Protection from
    startup imports such as ``sitecustomize`` comes from the fixed ``-I``
    interpreter argument in the shebang and every supported launch command.
    """
    if sys.flags.isolated != 1:
        _fail(
            "release ceremony must be launched with /usr/bin/python3 -I -B"
        )


def _directory_identity(info: os.stat_result) -> tuple[int, ...]:
    return (info.st_dev, info.st_ino)


@dataclasses.dataclass(frozen=True)
class ControlledOutput:
    parent: Path
    directory_fd: int
    directory_identity: tuple[int, ...]
    output_name: str
    temporary_name: str

    @property
    def path(self) -> Path:
        return self.parent / self.output_name


def _directory_entry_absent(
    directory_fd: int, name: str, description: str
) -> None:
    try:
        os.stat(name, dir_fd=directory_fd, follow_symlinks=False)
    except FileNotFoundError:
        return
    except OSError as error:
        raise PackagerError(f"cannot inspect {description}") from error
    _fail(f"{description} already exists")


def _recheck_controlled_output(target: ControlledOutput) -> None:
    try:
        descriptor_info = os.fstat(target.directory_fd)
        path_info = os.lstat(target.parent)
    except OSError as error:
        raise PackagerError("release output parent became unavailable") from error
    if (
        _directory_identity(descriptor_info) != target.directory_identity
        or _directory_identity(path_info) != target.directory_identity
    ):
        _fail("release output parent changed during the ceremony")


def _temporary_output_name(output_name: str) -> str:
    return f".{output_name}.tmp-{os.getpid()}-{os.urandom(16).hex()}"


def _prepare_controlled_output(value: str) -> ControlledOutput:
    output = Path(value)
    if (
        not output.is_absolute()
        or output.name in ("", ".", "..")
        or os.path.normpath(os.fspath(output)) != os.fspath(output)
    ):
        _fail("release output path must be absolute and canonical")
    parent = output.parent
    try:
        path_info = os.stat(parent)
    except OSError as error:
        raise PackagerError(
            "release output parent must already exist"
        ) from error
    if (
        not stat.S_ISDIR(path_info.st_mode)
    ):
        _fail("release output parent must be a directory")
    try:
        directory_fd = os.open(
            parent,
            os.O_RDONLY
            | os.O_DIRECTORY
            | os.O_CLOEXEC,
        )
    except OSError as error:
        raise PackagerError("release output parent open failed") from error
    try:
        descriptor_info = os.fstat(directory_fd)
        identity = _directory_identity(path_info)
        if _directory_identity(descriptor_info) != identity:
            _fail("release output parent changed while opening")
        temporary_name = _temporary_output_name(output.name)
        if temporary_name in ("", ".", "..") or "/" in temporary_name:
            _fail("release temporary output name rejected")
        _directory_entry_absent(
            directory_fd, output.name, "release output"
        )
        _directory_entry_absent(
            directory_fd, temporary_name, "release temporary output"
        )
        return ControlledOutput(
            parent,
            directory_fd,
            identity,
            output.name,
            temporary_name,
        )
    except BaseException:
        os.close(directory_fd)
        raise


def _validate_output_before_secret(target: ControlledOutput) -> None:
    """Repeat every output invariant immediately before consuming fd 3."""
    _recheck_controlled_output(target)
    _directory_entry_absent(
        target.directory_fd, target.output_name, "release output"
    )
    _directory_entry_absent(
        target.directory_fd,
        target.temporary_name,
        "release temporary output",
    )


def _rename_output_noreplace(
    directory_fd: int, old_name: str, new_name: str
) -> None:
    renameat2 = getattr(ctypes.CDLL(None, use_errno=True), "renameat2", None)
    if renameat2 is None:
        _fail("renameat2(RENAME_NOREPLACE) is required for release output")
    result = renameat2(
        ctypes.c_int(directory_fd),
        ctypes.c_char_p(os.fsencode(old_name)),
        ctypes.c_int(directory_fd),
        ctypes.c_char_p(os.fsencode(new_name)),
        ctypes.c_uint(1),
    )
    if result != 0:
        error_number = ctypes.get_errno()
        raise PackagerError(
            "atomic no-replace release publication failed"
        ) from OSError(error_number, os.strerror(error_number))


def _unlink_owned_output(
    target: ControlledOutput,
    name: str,
    expected: os.stat_result,
) -> None:
    try:
        current = os.stat(
            name, dir_fd=target.directory_fd, follow_symlinks=False
        )
    except FileNotFoundError:
        return
    except OSError:
        return
    if (
        current.st_dev,
        current.st_ino,
    ) != (
        expected.st_dev,
        expected.st_ino,
    ):
        return
    try:
        os.unlink(name, dir_fd=target.directory_fd)
    except OSError:
        pass


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while True:
            block = stream.read(1024 * 1024)
            if not block:
                break
            digest.update(block)
    return digest.hexdigest()


def _metadata_snapshot(info: os.stat_result) -> tuple[int, ...]:
    return (
        info.st_dev,
        info.st_ino,
        info.st_size,
        info.st_mtime_ns,
    )


def _checked_file_bytes(path: Path, maximum_size: int, description: str) -> bytes:
    info = os.stat(path)
    if (
        not stat.S_ISREG(info.st_mode)
        or info.st_size <= 0
        or info.st_size > maximum_size
    ):
        _fail(f"{description} type or size rejected")
    fd = os.open(path, os.O_RDONLY | os.O_CLOEXEC)
    try:
        before = os.fstat(fd)
        if _metadata_snapshot(before) != _metadata_snapshot(info):
            _fail(f"{description} changed before open")
        output = bytearray()
        while len(output) < before.st_size:
            block = os.read(fd, min(1024 * 1024, before.st_size - len(output)))
            if not block:
                _fail(f"{description} was truncated while reading")
            output.extend(block)
        if os.read(fd, 1):
            _fail(f"{description} grew while reading")
        after = os.fstat(fd)
    finally:
        os.close(fd)
    current = os.stat(path)
    if (
        _metadata_snapshot(after) != _metadata_snapshot(before)
        or _metadata_snapshot(current) != _metadata_snapshot(before)
    ):
        _fail(f"{description} changed while reading")
    return bytes(output)


def _run(
    arguments: Sequence[str],
    *,
    pass_fds: Sequence[int] = (),
    input_data: bytes | None = None,
    timeout_seconds: int = 30,
    trusted_error_output: bool = False,
) -> bytes:
    try:
        result = subprocess.run(
            list(arguments),
            input=input_data,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            close_fds=True,
            pass_fds=tuple(pass_fds),
            env={"LC_ALL": "C", "PATH": "/usr/bin:/bin"},
            timeout=timeout_seconds,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise PackagerError(f"release tool failed: {arguments[0]}") from error
    if result.returncode != 0:
        detail = ""
        if trusted_error_output:
            detail_text = result.stderr[:2048].decode(
                "utf-8",
                "backslashreplace",
            ).strip()
            if detail_text:
                detail = f": {detail_text}"
        _fail(
            "release tool rejected input: "
            f"{Path(arguments[0]).name} (exit {result.returncode}){detail}"
        )
    return result.stdout


def _signing_key_fd_is_inherited() -> bool:
    try:
        os.fstat(3)
    except OSError as error:
        if error.errno == errno.EBADF:
            return False
        raise PackagerError(
            "release signing key fd could not be inspected"
        ) from error
    return True


def _read_signing_key_fd(inherited: bool) -> bytearray:
    if not inherited:
        raise PackagerError(
            "release signing key must be supplied on inherited fd 3"
        )
    try:
        info = os.fstat(3)
    except OSError as error:
        raise PackagerError("release signing key must be supplied on inherited fd 3") from error
    key = bytearray()
    try:
        is_pipe = False
        if stat.S_ISFIFO(info.st_mode):
            try:
                descriptor_link = os.readlink("/proc/self/fd/3")
            except OSError:
                descriptor_link = ""
            is_pipe = (
                re.fullmatch(r"pipe:\[[1-9][0-9]*\]", descriptor_link)
                is not None
            )
        is_sealed_memfd = False
        if stat.S_ISREG(info.st_mode) and info.st_nlink == 0:
            try:
                seals = fcntl.fcntl(3, fcntl.F_GET_SEALS)
            except OSError:
                seals = 0
            required = (
                fcntl.F_SEAL_SEAL
                | fcntl.F_SEAL_SHRINK
                | fcntl.F_SEAL_GROW
                | fcntl.F_SEAL_WRITE
            )
            is_sealed_memfd = seals & required == required
        if not (is_pipe or is_sealed_memfd):
            _fail(
                "release signing key fd must be an anonymous pipe or fully "
                "sealed memfd"
            )
        while len(key) <= 16 * 1024:
            block = os.read(
                3,
                min(4096, 16 * 1024 + 1 - len(key)),
            )
            if not block:
                break
            key.extend(block)
    except BaseException:
        for index in range(len(key)):
            key[index] = 0
        raise
    finally:
        # fd 3 is a one-shot ceremony channel.  In particular, close a sealed
        # memfd after consumption so later code cannot seek back to the key.
        try:
            os.close(3)
        except OSError:
            pass
    if not key or len(key) > 16 * 1024:
        for index in range(len(key)):
            key[index] = 0
        _fail("release signing key length rejected")
    return key


def _sealed_memfd(data: bytearray) -> int:
    if not hasattr(os, "memfd_create"):
        _fail("memfd_create is required for release signing")
    fd = os.memfd_create("cosmo-release-signing-key", os.MFD_CLOEXEC | os.MFD_ALLOW_SEALING)
    try:
        offset = 0
        while offset < len(data):
            offset += os.write(fd, data[offset:])
        os.lseek(fd, 0, os.SEEK_SET)
        seals = fcntl.F_SEAL_SEAL | fcntl.F_SEAL_SHRINK | fcntl.F_SEAL_GROW | fcntl.F_SEAL_WRITE
        fcntl.fcntl(fd, fcntl.F_ADD_SEALS, seals)
        return fd
    except BaseException:
        os.close(fd)
        raise
    finally:
        for index in range(len(data)):
            data[index] = 0


def _readonly_memfd(name: str, data: bytes) -> int:
    if not hasattr(os, "memfd_create"):
        _fail("memfd_create is required for release signing")
    fd = os.memfd_create(name, os.MFD_CLOEXEC | os.MFD_ALLOW_SEALING)
    try:
        offset = 0
        while offset < len(data):
            offset += os.write(fd, data[offset:])
        os.lseek(fd, 0, os.SEEK_SET)
        seals = fcntl.F_SEAL_SEAL | fcntl.F_SEAL_SHRINK | fcntl.F_SEAL_GROW | fcntl.F_SEAL_WRITE
        fcntl.fcntl(fd, fcntl.F_ADD_SEALS, seals)
        return fd
    except BaseException:
        os.close(fd)
        raise


def _canonical_ed25519_public_key(
    openssl: Path,
    public_key: Path,
    description: str,
) -> tuple[bytes, str, bytes]:
    pem = _checked_file_bytes(public_key, 16 * 1024, description)
    pem_fd = _readonly_memfd("cosmo-release-public-key", pem)
    try:
        key_path = f"/proc/self/fd/{pem_fd}"
        canonical_pem = _run(
            (str(openssl), "pkey", "-pubin", "-in", key_path, "-pubout"),
            pass_fds=(pem_fd,),
        )
        os.lseek(pem_fd, 0, os.SEEK_SET)
        der = _run(
            (
                str(openssl),
                "pkey",
                "-pubin",
                "-in",
                key_path,
                "-outform",
                "DER",
            ),
            pass_fds=(pem_fd,),
        )
    finally:
        os.close(pem_fd)
    if pem != canonical_pem:
        _fail(
            f"{description} PEM is not in the canonical OpenSSL encoding"
        )
    # RFC 8410 Ed25519 SubjectPublicKeyInfo is 44 bytes and has this fixed prefix.
    if len(der) != 44 or der[:12] != bytes.fromhex("302a300506032b6570032100"):
        _fail(
            f"{description} is not a canonical RFC 8410 Ed25519 "
            "SubjectPublicKeyInfo"
        )
    if not any(der[-32:]):
        _fail(f"{description} raw Ed25519 key is zero")
    return der[-32:], hashlib.sha256(pem).hexdigest(), pem


def _public_key_identity(
    openssl: Path, public_key: Path
) -> tuple[str, str, dict[str, bytes], bytes]:
    raw_public_key, pem_sha256, pem = _canonical_ed25519_public_key(
        openssl,
        public_key,
        "release public key",
    )
    key_id = hashlib.sha256(
        b"cosmo-release-key-id-v1"
        + (1).to_bytes(2, "big")
        + raw_public_key
    ).hexdigest()[:32]
    if set(key_id) == {"0"}:
        _fail("derived release key ID is zero")
    values = {
        "cosmo_release_public_key_raw_v1": raw_public_key,
        "cosmo_release_public_key_id_v1": bytes.fromhex(key_id),
        "cosmo_release_public_key_pem_sha256_v1": hashlib.sha256(pem).digest(),
    }
    return key_id, pem_sha256, values, pem


def _private_elf_object_symbols(
    path: Path,
    expected_sizes: Mapping[str, int],
    *,
    description: str,
    maximum_size: int,
    expected_binding: int,
    expected_visibility: int,
) -> dict[str, bytes]:
    """Read exact private objects from an AArch64 ELF without executing it."""
    info = os.stat(path)
    if (
        not stat.S_ISREG(info.st_mode)
        or info.st_size < 64
        or info.st_size > maximum_size
    ):
        _fail(f"{description} ELF type or size rejected")
    data = path.read_bytes()
    if len(data) != info.st_size:
        _fail(f"{description} ELF changed while reading")
    if data[:16] != b"\x7fELF\x02\x01\x01\x00" + b"\x00" * 8:
        _fail(f"{description} must be a little-endian ELF64 image")
    try:
        header = struct.unpack_from("<16sHHIQQQIHHHHHH", data, 0)
    except struct.error as error:
        raise PackagerError(f"{description} ELF header is truncated") from error
    (
        _,
        elf_type,
        machine,
        version,
        _,
        _,
        section_offset,
        _,
        elf_header_size,
        _,
        _,
        section_entry_size,
        section_count,
        section_name_index,
    ) = header
    if (
        elf_type not in (2, 3)
        or machine != 183
        or version != 1
        or elf_header_size != 64
        or section_entry_size != 64
        or section_count == 0
        or section_name_index >= section_count
        or section_offset > len(data)
        or section_count > (len(data) - section_offset) // section_entry_size
    ):
        _fail(f"{description} AArch64 ELF section header rejected")

    sections: list[tuple[int, int, int, int, int, int, int, int, int, int]] = []
    try:
        for index in range(section_count):
            sections.append(
                struct.unpack_from(
                    "<IIQQQQIIQQ", data, section_offset + index * section_entry_size
                )
            )
    except struct.error as error:
        raise PackagerError(f"{description} ELF section table is truncated") from error
    for _, section_type, _, _, offset, size, _, _, _, _ in sections:
        if section_type != 8 and (offset > len(data) or size > len(data) - offset):
            _fail(f"{description} ELF section extent rejected")

    found: dict[str, bytes] = {}
    encoded_names = {candidate.encode("ascii") for candidate in expected_sizes}
    for section in sections:
        _, section_type, _, _, offset, size, string_index, _, _, entry_size = section
        if section_type not in (2, 11):
            continue
        if (
            entry_size != 24
            or size % entry_size != 0
            or string_index >= len(sections)
            or sections[string_index][1] != 3
        ):
            _fail(f"{description} ELF symbol table rejected")
        string_offset = sections[string_index][4]
        string_size = sections[string_index][5]
        strings = data[string_offset : string_offset + string_size]
        for symbol_offset in range(offset, offset + size, entry_size):
            try:
                name_offset, symbol_info, symbol_other, symbol_section, value, symbol_size = (
                    struct.unpack_from("<IBBHQQ", data, symbol_offset)
                )
            except struct.error as error:
                raise PackagerError(f"{description} ELF symbol entry is truncated") from error
            if name_offset >= len(strings):
                _fail(f"{description} ELF symbol name offset rejected")
            name_end = strings.find(b"\x00", name_offset)
            if name_end < 0:
                _fail(f"{description} ELF symbol name is unterminated")
            name_bytes = strings[name_offset:name_end]
            if name_bytes not in encoded_names:
                continue
            name = name_bytes.decode("ascii")
            if section_type == 11:
                _fail(f"{description} exposes a private trust symbol dynamically")
            if name in found:
                _fail(f"{description} contains a duplicate trust symbol")
            if (
                symbol_info >> 4 != expected_binding
                or symbol_info & 0xF != 1
                or symbol_other != expected_visibility
                or symbol_size != expected_sizes[name]
                or symbol_section == 0
                or symbol_section >= len(sections)
            ):
                _fail(f"{description} trust symbol metadata rejected")
            target = sections[symbol_section]
            _, target_type, target_flags, target_address, target_offset, target_size, _, _, _, _ = target
            if (
                target_type != 1
                or target_flags & 0x2 == 0
                or target_flags & (0x1 | 0x4)
                or value < target_address
                or value - target_address > target_size
                or symbol_size > target_size - (value - target_address)
            ):
                _fail(f"{description} trust symbol section rejected")
            file_offset = target_offset + value - target_address
            if file_offset > len(data) or symbol_size > len(data) - file_offset:
                _fail(f"{description} trust symbol extent rejected")
            found[name] = data[file_offset : file_offset + symbol_size]
    if set(found) != set(expected_sizes):
        _fail(f"{description} is missing its private trust symbols")
    return found


def _bootstrap_trust_symbols(path: Path) -> dict[str, bytes]:
    return _private_elf_object_symbols(
        path,
        BOOTSTRAP_TRUST_SYMBOL_SIZES,
        description="release bootstrap",
        maximum_size=MAX_BOOTSTRAP_ELF_BYTES,
        expected_binding=1,  # STB_GLOBAL
        expected_visibility=2,  # STV_HIDDEN
    )


def _parse_elf64(
    data: bytes,
    *,
    description: str,
    expected_type: int,
    require_program_headers: bool,
) -> tuple[list[dict[str, int | bytes]], list[dict[str, int]]]:
    if len(data) < 64 or len(data) > MAX_MODEL_GUARD_ELF_BYTES:
        _fail(f"{description} ELF size rejected")
    if data[:16] != b"\x7fELF\x02\x01\x01\x00" + b"\x00" * 8:
        _fail(f"{description} must be a little-endian ELF64 image")
    try:
        header = struct.unpack_from("<16sHHIQQQIHHHHHH", data, 0)
    except struct.error as error:
        raise PackagerError(f"{description} ELF header is truncated") from error
    (
        _,
        elf_type,
        machine,
        version,
        _,
        program_offset,
        section_offset,
        _,
        elf_header_size,
        program_entry_size,
        program_count,
        section_entry_size,
        section_count,
        section_name_index,
    ) = header
    if (
        elf_type != expected_type
        or machine != 183
        or version != 1
        or elf_header_size != 64
        or section_entry_size != 64
        or section_count == 0
        or section_count > 4096
        or section_name_index >= section_count
        or section_offset > len(data)
        or section_count > (len(data) - section_offset) // section_entry_size
    ):
        _fail(f"{description} AArch64 ELF header rejected")
    if require_program_headers:
        if (
            program_entry_size != 56
            or program_count == 0
            or program_count > 512
            or program_offset > len(data)
            or program_count > (len(data) - program_offset) // program_entry_size
        ):
            _fail(f"{description} ELF program header rejected")
    elif program_count != 0:
        if (
            program_entry_size != 56
            or program_count > 512
            or program_offset > len(data)
            or program_count > (len(data) - program_offset) // program_entry_size
        ):
            _fail(f"{description} ELF program header rejected")

    raw_sections: list[tuple[int, int, int, int, int, int, int, int, int, int]] = []
    try:
        for index in range(section_count):
            raw_sections.append(
                struct.unpack_from(
                    "<IIQQQQIIQQ", data, section_offset + index * section_entry_size
                )
            )
    except struct.error as error:
        raise PackagerError(f"{description} ELF section table is truncated") from error
    for _, section_type, _, _, offset, size, _, _, _, _ in raw_sections:
        if section_type != 8 and (offset > len(data) or size > len(data) - offset):
            _fail(f"{description} ELF section extent rejected")
    name_section = raw_sections[section_name_index]
    if name_section[1] != 3:
        _fail(f"{description} ELF section-name table rejected")
    name_data = data[name_section[4] : name_section[4] + name_section[5]]
    sections: list[dict[str, int | bytes]] = []
    for section in raw_sections:
        name_offset = section[0]
        if name_offset >= len(name_data):
            _fail(f"{description} ELF section name offset rejected")
        name_end = name_data.find(b"\x00", name_offset)
        if name_end < 0:
            _fail(f"{description} ELF section name is unterminated")
        sections.append(
            {
                "name": name_data[name_offset:name_end],
                "type": section[1],
                "flags": section[2],
                "address": section[3],
                "offset": section[4],
                "size": section[5],
                "link": section[6],
                "info": section[7],
                "alignment": section[8],
                "entry_size": section[9],
            }
        )

    programs: list[dict[str, int]] = []
    try:
        for index in range(program_count):
            values = struct.unpack_from(
                "<IIQQQQQQ", data, program_offset + index * program_entry_size
            )
            programs.append(
                {
                    "type": values[0],
                    "flags": values[1],
                    "offset": values[2],
                    "vaddr": values[3],
                    "file_size": values[5],
                    "memory_size": values[6],
                    "alignment": values[7],
                }
            )
    except struct.error as error:
        raise PackagerError(f"{description} ELF program table is truncated") from error
    for program in programs:
        offset = program["offset"]
        file_size = program["file_size"]
        if (
            offset > len(data)
            or file_size > len(data) - offset
            or program["memory_size"] < file_size
        ):
            _fail(f"{description} ELF program extent rejected")
    return sections, programs


def _model_guard_trust_sections(data: bytes) -> dict[str, bytes]:
    sections, programs = _parse_elf64(
        data,
        description="release Model Guard",
        expected_type=3,  # ET_DYN
        require_program_headers=True,
    )
    found: dict[str, bytes] = {}
    extents: list[tuple[int, int]] = []
    address_extents: list[tuple[int, int]] = []
    if any(section["name"] == b".symtab" for section in sections):
        _fail("release Model Guard must be stripped of its static symbol table")
    for logical_name, (section_name, expected_size) in MODEL_GUARD_TRUST_SECTIONS.items():
        encoded_name = section_name.encode("ascii")
        matches = [section for section in sections if section["name"] == encoded_name]
        if len(matches) != 1:
            _fail(f"release Model Guard trust section count rejected: {section_name}")
        section = matches[0]
        if (
            section["type"] != 1
            or section["flags"] != 0x2
            or section["size"] != expected_size
            or section["alignment"] not in (1, 2, 4, 8, 16)
        ):
            _fail(f"release Model Guard trust section metadata rejected: {section_name}")
        section_offset = int(section["offset"])
        section_address = int(section["address"])
        covering_loads = []
        for program in programs:
            if program["type"] != 1:
                continue
            if (
                program["offset"] <= section_offset
                and expected_size <= program["file_size"] - (section_offset - program["offset"])
                and program["vaddr"] <= section_address
                and expected_size
                <= program["memory_size"] - (section_address - program["vaddr"])
                and section_offset - program["offset"]
                == section_address - program["vaddr"]
            ):
                covering_loads.append(program)
        if len(covering_loads) != 1 or covering_loads[0]["flags"] != 0x4:
            _fail(
                "release Model Guard trust section is not in exactly one "
                f"read-only non-executable PT_LOAD: {section_name}"
            )
        owner = covering_loads[0]
        page_size = 0x10000
        trust_page_start = section_address & ~(page_size - 1)
        trust_page_end = (
            section_address + expected_size + page_size - 1
        ) & ~(page_size - 1)
        for program in programs:
            if program["type"] != 1 or program is owner:
                continue
            program_start = program["vaddr"] & ~(page_size - 1)
            program_end = (
                program["vaddr"] + program["memory_size"] + page_size - 1
            ) & ~(page_size - 1)
            if program_start < trust_page_end and trust_page_start < program_end:
                _fail(
                    "release Model Guard trust section overlaps another "
                    f"PT_LOAD at BM1688 page granularity: {section_name}"
                )
        extent = (section_offset, section_offset + expected_size)
        if any(extent[0] < other[1] and other[0] < extent[1] for other in extents):
            _fail("release Model Guard trust sections overlap")
        extents.append(extent)
        address_extents.append(
            (section_address, section_address + expected_size)
        )
        found[logical_name] = data[extent[0] : extent[1]]

    # A trust section that is read-only in the file is not immutable at runtime
    # if the dynamic loader is instructed to rewrite it.  The loader follows
    # PT_DYNAMIC, not section-header metadata, so derive every active relocation
    # table from the unique PT_DYNAMIC program header.
    dynamic_programs = [program for program in programs if program["type"] == 2]
    if len(dynamic_programs) != 1:
        _fail("release Model Guard must have exactly one PT_DYNAMIC")
    dynamic_program = dynamic_programs[0]
    if dynamic_program["file_size"] % 16 != 0:
        _fail("release Model Guard PT_DYNAMIC extent rejected")
    dynamic_entries: list[tuple[int, int]] = []
    found_dynamic_end = False
    for offset in range(
        dynamic_program["offset"],
        dynamic_program["offset"] + dynamic_program["file_size"],
        16,
    ):
        tag, value = struct.unpack_from("<qQ", data, offset)
        if tag == 0:
            found_dynamic_end = True
            break
        dynamic_entries.append((tag, value))
    if not found_dynamic_end:
        _fail("release Model Guard PT_DYNAMIC is unterminated")
    if any(
        tag == 22 or (tag == 30 and value & 0x4)
        for tag, value in dynamic_entries
    ):
        _fail("release Model Guard dynamic text relocations are forbidden")
    if any(tag in (35, 36, 37) for tag, _ in dynamic_entries):
        _fail("release Model Guard packed relative relocations are forbidden")

    relevant_tags = {2, 7, 8, 9, 17, 18, 19, 20, 23}
    dynamic_values: dict[int, int] = {}
    for tag, value in dynamic_entries:
        if tag not in relevant_tags:
            continue
        if tag in dynamic_values:
            _fail("release Model Guard dynamic relocation tag is duplicated")
        dynamic_values[tag] = value

    def relocation_bytes(address: int, size: int) -> bytes:
        matches = [
            program
            for program in programs
            if program["type"] == 1
            and program["vaddr"] <= address
            and size <= program["file_size"] - (address - program["vaddr"])
        ]
        if len(matches) != 1:
            _fail("release Model Guard relocation table mapping rejected")
        program = matches[0]
        offset = program["offset"] + address - program["vaddr"]
        return data[offset : offset + size]

    def inspect_relocations(address_tag: int, size_tag: int, entry_tag: int, expected: int) -> None:
        present = [tag in dynamic_values for tag in (address_tag, size_tag, entry_tag)]
        if not any(present):
            return
        if not all(present):
            _fail("release Model Guard dynamic relocation contract is incomplete")
        size = dynamic_values[size_tag]
        if dynamic_values[entry_tag] != expected or size % expected != 0:
            _fail("release Model Guard dynamic relocation entry size rejected")
        table = relocation_bytes(dynamic_values[address_tag], size)
        for offset in range(0, len(table), expected):
            relocation_target = struct.unpack_from("<Q", table, offset)[0]
            if any(
                relocation_target < end and start < relocation_target + 16
                for start, end in address_extents
            ):
                _fail("release Model Guard trust section has a dynamic relocation")

    inspect_relocations(7, 8, 9, 24)   # DT_RELA/DT_RELASZ/DT_RELAENT
    inspect_relocations(17, 18, 19, 16)  # DT_REL/DT_RELSZ/DT_RELENT
    if any(tag in dynamic_values for tag in (2, 20, 23)):
        if not all(tag in dynamic_values for tag in (2, 20, 23)):
            _fail("release Model Guard PLT relocation contract is incomplete")
        plt_entry_size = {7: 24, 17: 16}.get(dynamic_values[20])
        if plt_entry_size is None or dynamic_values[2] % plt_entry_size != 0:
            _fail("release Model Guard PLT relocation format rejected")
        table = relocation_bytes(dynamic_values[23], dynamic_values[2])
        for offset in range(0, len(table), plt_entry_size):
            relocation_target = struct.unpack_from("<Q", table, offset)[0]
            if any(
                relocation_target < end and start < relocation_target + 16
                for start, end in address_extents
            ):
                _fail("release Model Guard trust section has a dynamic relocation")

    # Section tables are still checked as a defense-in-depth consistency gate,
    # but they are not the source of truth for loader behavior.
    for section in sections:
        section_type = int(section["type"])
        section_flags = int(section["flags"])
        section_offset = int(section["offset"])
        section_size = int(section["size"])
        entry_size = int(section["entry_size"])
        if section_type not in (4, 9) or not section_flags & 0x2:
            continue
        required_entry_size = 24 if section_type == 4 else 16
        if entry_size != required_entry_size or section_size % entry_size != 0:
            _fail("release Model Guard dynamic relocation table rejected")
        for offset in range(
            section_offset, section_offset + section_size, entry_size
        ):
            relocation_target = struct.unpack_from("<Q", data, offset)[0]
            if any(
                relocation_target < end and start < relocation_target + 16
                for start, end in address_extents
            ):
                _fail("release Model Guard trust section has a dynamic relocation")
    return found


def release_public_key_identity_from_object(data: bytes) -> tuple[str, bytes]:
    sections, _ = _parse_elf64(
        data,
        description="release public-key object",
        expected_type=1,  # ET_REL
        require_program_headers=False,
    )
    expected_name = b".rodata.cosmo_release_key"
    matches = [section for section in sections if section["name"] == expected_name]
    if len(matches) != 1:
        _fail("release public-key object trust section count rejected")
    section = matches[0]
    if (
        section["type"] != 1
        or section["flags"] != 0x2
        or section["address"] != 0
        or section["size"] != 80
        or section["alignment"] not in (1, 2, 4, 8, 16)
    ):
        _fail("release public-key object trust section metadata rejected")
    for candidate in sections:
        if (
            candidate is not section
            and candidate["size"] != 0
            and int(candidate["flags"]) & 0x2
        ):
            _fail("release public-key object has unexpected allocated content")
    offset = int(section["offset"])
    value = data[offset : offset + 80]
    raw_key, encoded_key_id, pem_sha256 = value[:32], value[32:48], value[48:]
    key_id = hashlib.sha256(
        b"cosmo-release-key-id-v1" + (1).to_bytes(2, "big") + raw_key
    ).digest()[:16]
    der = bytes.fromhex("302a300506032b6570032100") + raw_key
    encoded = base64.b64encode(der)
    pem = b"-----BEGIN PUBLIC KEY-----\n" + encoded + b"\n-----END PUBLIC KEY-----\n"
    if (
        not any(raw_key)
        or not hmac.compare_digest(key_id, encoded_key_id)
        or not hmac.compare_digest(hashlib.sha256(pem).digest(), pem_sha256)
    ):
        _fail("release public-key object identity rejected")
    return key_id.hex(), pem

def _verify_ed25519_signature(
    openssl: Path,
    public_key_pem: bytes,
    message: bytes,
    signature: bytes,
    description: str,
) -> None:
    if len(signature) != 64:
        _fail(f"{description} signature length rejected")
    try:
        with contextlib.ExitStack() as descriptors:
            key_fd = _readonly_memfd(
                "cosmo-signature-public-key", public_key_pem
            )
            descriptors.callback(os.close, key_fd)
            message_fd = _readonly_memfd(
                "cosmo-signature-message", message
            )
            descriptors.callback(os.close, message_fd)
            signature_fd = _readonly_memfd(
                "cosmo-signature-value", signature
            )
            descriptors.callback(os.close, signature_fd)
            _run(
                (
                    str(openssl),
                    "pkeyutl",
                    "-verify",
                    "-pubin",
                    "-inkey",
                    f"/proc/self/fd/{key_fd}",
                    "-rawin",
                    "-in",
                    f"/proc/self/fd/{message_fd}",
                    "-sigfile",
                    f"/proc/self/fd/{signature_fd}",
                ),
                pass_fds=(key_fd, message_fd, signature_fd),
            )
    except PackagerError as error:
        raise PackagerError(f"{description} signature rejected") from error


def _validate_product_pepper_bundle(value: bytes) -> None:
    if (
        len(value) != 64
        or value[:4] != b"CMPB"
        or value[4:6] != b"\x00\x01"
        or value[6:8] != b"\x00\x40"
        or any(value[8:16])
    ):
        _fail("release Model Guard product-pepper bundle format rejected")
    key_id = value[16:32]
    pepper = value[32:64]
    derived = hashlib.sha256(
        b"cosmo-product-pepper-key-id-v1" + pepper
    ).digest()[:16]
    if not any(key_id) or not any(pepper) or key_id != derived:
        _fail("release Model Guard product-pepper record rejected")


def _validate_commissioning_public_key_bundle(value: bytes) -> None:
    if (
        len(value) != 64
        or value[:4] != b"CMKB"
        or value[4:6] != b"\x00\x01"
        or value[6:8] != b"\x00\x40"
        or any(value[8:16])
    ):
        _fail("release Model Guard commissioning-key bundle format rejected")
    key_id = value[16:32]
    public_key = value[32:64]
    derived = hashlib.sha256(
        b"cosmo-commissioning-key-id-v1" + b"\x00\x01" + public_key
    ).digest()[:16]
    if not any(key_id) or not any(public_key) or key_id != derived:
        _fail("release Model Guard commissioning-key record rejected")


def audit_model_guard_trust_objects(image: bytes) -> dict[str, bytes]:
    values = _model_guard_trust_sections(image)
    _validate_product_pepper_bundle(values["product_pepper_bundle"])
    _validate_commissioning_public_key_bundle(
        values["commissioning_public_key_bundle"]
    )
    return values


def _audit_bootstrap_dynamic_contract(path: Path, readelf: Path) -> None:
    dynamic = schema._run_tool((str(readelf), "-dW", str(path))).decode(
        "utf-8", "replace"
    )
    needed = re.findall(r"\(NEEDED\).*\[([^\]]+)\]", dynamic)
    runpaths = re.findall(r"\(RUNPATH\).*\[([^\]]+)\]", dynamic)
    if runpaths != [EXPECTED_BOOTSTRAP_RUNPATH] or "(RPATH)" in dynamic:
        _fail("release bootstrap RUNPATH/RPATH contract rejected")
    needed_set = set(needed)
    if (
        len(needed_set) != len(needed)
        or not BOOTSTRAP_REQUIRED_NEEDED.issubset(needed_set)
        or not needed_set.issubset(BOOTSTRAP_ALLOWED_NEEDED)
    ):
        _fail(f"release bootstrap NEEDED set rejected: {sorted(needed_set)}")


def _collect_payload(payload: Path) -> tuple[list[Mapping[str, Any]], dict[str, Path]]:
    if payload.is_symlink() or not payload.is_dir():
        _fail("payload root must be a real directory")
    entries: list[Mapping[str, Any]] = []
    sources: dict[str, Path] = {}
    for root, directories, files in os.walk(payload, topdown=True, followlinks=False):
        directories.sort(key=lambda item: item.encode("utf-8"))
        files.sort(key=lambda item: item.encode("utf-8"))
        current = Path(root)
        for name in list(directories):
            path = current / name
            relative = path.relative_to(payload).as_posix()
            if name == TEST_FIXTURE_MARKER_NAME:
                _fail(f"Model Guard test-fixture marker is forbidden in a release: {relative}")
            if name == "__pycache__":
                _fail(f"Python bytecode cache directory is forbidden in a release: {relative}")
            schema._canonical_relative_path(relative, "payload directory")
            info = os.lstat(path)
            if stat.S_ISLNK(info.st_mode):
                directories.remove(name)
                target = schema._safe_symlink_target(relative, os.readlink(path))
                entries.append({"path": relative, "target": target, "type": "symlink"})
                sources[relative] = path
            elif not stat.S_ISDIR(info.st_mode):
                _fail(f"payload directory entry has a forbidden type: {relative}")
            else:
                entries.append({"mode": 0o755, "path": relative, "type": "directory"})
                sources[relative] = path
        for name in files:
            path = current / name
            relative = path.relative_to(payload).as_posix()
            if name == TEST_FIXTURE_MARKER_NAME:
                _fail(f"Model Guard test-fixture marker is forbidden in a release: {relative}")
            if name.endswith((".pyc", ".pyo")):
                _fail(f"Python bytecode is forbidden in a release: {relative}")
            schema._canonical_relative_path(relative, "payload file")
            info = os.lstat(path)
            if stat.S_ISLNK(info.st_mode):
                target = schema._safe_symlink_target(relative, os.readlink(path))
                entries.append({"path": relative, "target": target, "type": "symlink"})
            elif stat.S_ISREG(info.st_mode):
                if (
                    info.st_size == len(TEST_FIXTURE_MARKER_CONTENT)
                    and path.read_bytes() == TEST_FIXTURE_MARKER_CONTENT
                ):
                    _fail(
                        "Model Guard test-fixture marker content is forbidden "
                        f"in a release: {relative}"
                    )
                mode = 0o755 if stat.S_IMODE(info.st_mode) & 0o111 else 0o644
                entries.append(
                    {
                        "mode": mode,
                        "path": relative,
                        "sha256": _sha256_file(path),
                        "size": info.st_size,
                        "type": "file",
                    }
                )
            else:
                _fail(f"payload object type rejected: {relative}")
            sources[relative] = path
    entries.sort(key=lambda entry: str(entry["path"]).encode("utf-8"))
    schema._validate_payload_manifest({"entries": entries, "format": schema.PAYLOAD_FORMAT})
    return entries, sources


def _copy_snapshot_regular(
    source: Path, destination: Path, expected: Mapping[str, Any]
) -> None:
    info = os.stat(source)
    if not stat.S_ISREG(info.st_mode):
        _fail(f"payload file changed type before snapshot: {expected['path']}")
    source_fd = os.open(source, os.O_RDONLY | os.O_CLOEXEC)
    destination_fd = os.open(
        destination,
        os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC | os.O_NOFOLLOW,
        0o600,
    )
    digest = hashlib.sha256()
    copied = 0
    try:
        before = os.fstat(source_fd)
        if _metadata_snapshot(before) != _metadata_snapshot(info):
            _fail(f"payload file changed before snapshot open: {expected['path']}")
        while True:
            block = os.read(source_fd, 1024 * 1024)
            if not block:
                break
            digest.update(block)
            copied += len(block)
            offset = 0
            while offset < len(block):
                offset += os.write(destination_fd, block[offset:])
        os.fdatasync(destination_fd)
        after = os.fstat(source_fd)
    finally:
        os.close(destination_fd)
        os.close(source_fd)
    current = os.stat(source)
    if (
        _metadata_snapshot(after) != _metadata_snapshot(before)
        or _metadata_snapshot(current) != _metadata_snapshot(before)
        or copied != expected["size"]
        or digest.hexdigest() != expected["sha256"]
    ):
        _fail(f"payload file changed while snapshotting: {expected['path']}")
    os.chmod(destination, int(expected["mode"]))


def _snapshot_payload(source: Path, destination: Path) -> None:
    entries, sources = _collect_payload(source)
    destination.mkdir(mode=0o700)
    for entry in entries:
        relative = str(entry["path"])
        target = destination / relative
        source_path = sources[relative]
        if entry["type"] == "directory":
            target.mkdir(mode=0o700)
        elif entry["type"] == "symlink":
            current_target = os.readlink(source_path)
            if current_target != entry["target"]:
                _fail(f"payload symlink changed while snapshotting: {relative}")
            os.symlink(current_target, target)
        else:
            _copy_snapshot_regular(source_path, target, entry)
    snapshot_entries, _ = _collect_payload(destination)
    if snapshot_entries != entries:
        _fail("private payload snapshot differs from the admitted source tree")


def _audit_for_manifest(
    payload: Path,
    nm: Path,
) -> tuple[list[str], str]:
    guard = payload / f"lib/{schema.GUARD_REAL_FILENAME}"
    output = schema._run_tool((str(nm), "-D", "--defined-only", "--format=posix", str(guard))).decode(
        "utf-8", "replace"
    )
    exports = sorted(
        fields[0].split("@", 1)[0]
        for line in output.splitlines()
        if (fields := line.split())
        and len(fields) >= 2
        and fields[1].upper() != "A"
        and not fields[0].startswith("_")
    )
    if exports != list(schema.REQUIRED_GUARD_EXPORTS):
        _fail("release guard exports do not match the frozen v2 ABI")
    guard_image = _checked_file_bytes(
        guard, MAX_MODEL_GUARD_ELF_BYTES, "release Model Guard"
    )
    audit_model_guard_trust_objects(guard_image)
    return exports, schema._exports_digest(exports)


def _tar_info(name: str, mode: int, object_type: bytes, size: int = 0, target: str = "") -> tarfile.TarInfo:
    info = tarfile.TarInfo(name)
    info.mode = mode
    info.uid = 0
    info.gid = 0
    info.uname = "root"
    info.gname = "root"
    info.mtime = 0
    info.type = object_type
    info.size = size
    info.linkname = target
    return info


def _write_bundle(
    output: ControlledOutput,
    manifest_bytes: bytes,
    signature: bytes,
    payload_bytes: bytes,
    entries: Sequence[Mapping[str, Any]],
    sources: Mapping[str, Path],
) -> None:
    _recheck_controlled_output(output)
    _directory_entry_absent(
        output.directory_fd, output.output_name, "release output"
    )
    _directory_entry_absent(
        output.directory_fd,
        output.temporary_name,
        "release temporary output",
    )
    fd = os.open(
        output.temporary_name,
        os.O_WRONLY
        | os.O_CREAT
        | os.O_EXCL
        | os.O_CLOEXEC
        | os.O_NOFOLLOW,
        0o600,
        dir_fd=output.directory_fd,
    )
    temporary_info = os.fstat(fd)
    try:
        with os.fdopen(fd, "wb", closefd=False) as raw, gzip.GzipFile(
            filename="", mode="wb", fileobj=raw, mtime=0, compresslevel=9
        ) as compressed, tarfile.open(fileobj=compressed, mode="w|", format=tarfile.PAX_FORMAT) as bundle:
            bundle.addfile(_tar_info("meta", 0o755, tarfile.DIRTYPE))
            for name, data in (
                ("meta/compatibility.manifest.json", manifest_bytes),
                ("meta/compatibility.manifest.sig", signature),
                ("meta/payload.files.json", payload_bytes),
            ):
                bundle.addfile(_tar_info(name, 0o600, tarfile.REGTYPE, len(data)), io.BytesIO(data))
            bundle.addfile(_tar_info("payload", 0o755, tarfile.DIRTYPE))
            for entry in entries:
                name = f"payload/{entry['path']}"
                if entry["type"] == "directory":
                    bundle.addfile(_tar_info(name, entry["mode"], tarfile.DIRTYPE))
                elif entry["type"] == "symlink":
                    bundle.addfile(_tar_info(name, 0o777, tarfile.SYMTYPE, target=entry["target"]))
                else:
                    with sources[str(entry["path"])].open("rb") as source:
                        bundle.addfile(_tar_info(name, entry["mode"], tarfile.REGTYPE, entry["size"]), source)
        os.fdatasync(fd)
    except BaseException:
        _unlink_owned_output(
            output, output.temporary_name, temporary_info
        )
        raise
    finally:
        os.close(fd)
    published = False
    try:
        _recheck_controlled_output(output)
        current_temporary = os.stat(
            output.temporary_name,
            dir_fd=output.directory_fd,
            follow_symlinks=False,
        )
        if (
            current_temporary.st_dev,
            current_temporary.st_ino,
        ) != (
            temporary_info.st_dev,
            temporary_info.st_ino,
        ):
            _fail("release temporary output changed before publication")
        _directory_entry_absent(
            output.directory_fd, output.output_name, "release output"
        )
        _rename_output_noreplace(
            output.directory_fd,
            output.temporary_name,
            output.output_name,
        )
        published = True
        published_info = os.stat(
            output.output_name,
            dir_fd=output.directory_fd,
            follow_symlinks=False,
        )
        if (
            published_info.st_dev,
            published_info.st_ino,
            published_info.st_mode,
        ) != (
            temporary_info.st_dev,
            temporary_info.st_ino,
            temporary_info.st_mode,
        ):
            _fail("published release output identity mismatch")
        os.fsync(output.directory_fd)
        _recheck_controlled_output(output)
    except BaseException:
        _unlink_owned_output(
            output,
            output.output_name if published else output.temporary_name,
            temporary_info,
        )
        try:
            os.fsync(output.directory_fd)
        except OSError:
            pass
        raise

def _build_bundle_from_snapshot(
    arguments: argparse.Namespace,
    payload: Path,
    output: ControlledOutput,
    public_key: Path,
    signing_key_fd_inherited: bool,
) -> None:
    openssl = Path("/usr/bin/openssl")
    readelf = Path("/usr/bin/aarch64-linux-gnu-readelf")
    nm = Path("/usr/bin/aarch64-linux-gnu-nm")
    for tool in (openssl, readelf, nm):
        if not tool.is_file():
            _fail(f"required fixed release tool is missing: {tool}")
        schema._validate_root_owned_tool(tool)

    release_id = schema._validate_release_id(arguments.release_id)
    generation = schema._require_uint(arguments.generation, "release generation", 1)
    entries, sources = _collect_payload(payload)
    payload_value = {"entries": entries, "format": schema.PAYLOAD_FORMAT}
    payload_bytes = schema._canonical_json(payload_value)
    key_id, key_sha256, expected_bootstrap_trust, public_key_pem = (
        _public_key_identity(openssl, public_key)
    )
    exports, exports_sha256 = _audit_for_manifest(payload, nm)

    regular_required_paths = {
        "bin/cosmo-engine",
        schema.RELEASE_BOOTSTRAP_PATH,
        schema.MODEL_PROVISION_PATH,
        f"lib/{schema.GUARD_REAL_FILENAME}",
        "lib/libbmrt.so",
        "lib/libbmrt.so.1.0",
        "lib/libbmlib.so",
        "lib/libbmlib.so.0",
        "lib/libcrypto.so.3",
        "lib/libssl.so.3",
        schema.GUARD_HEADER_PATH,
        *schema.REQUIRED_RELEASE_SCRIPTS,
    }
    symlink_targets = {
        f"lib/{schema.GUARD_SONAME}": schema.GUARD_REAL_FILENAME,
        "lib/libcosmo_model_guard.so": schema.GUARD_SONAME,
    }
    required_paths = regular_required_paths | set(symlink_targets)
    if not required_paths.issubset(sources):
        missing = ", ".join(sorted(required_paths - set(sources)))
        _fail(f"release payload is missing required compatibility files: {missing}")
    entry_by_path = {str(entry["path"]): entry for entry in entries}
    entry_types = {path: str(entry["type"]) for path, entry in entry_by_path.items()}
    for path in regular_required_paths:
        if entry_types.get(path) != "file":
            _fail(f"required release file must be a single-link regular file: {path}")
    for path, target in symlink_targets.items():
        if entry_types.get(path) != "symlink" or entry_by_path[path]["target"] != target:
            _fail(f"required release symlink target rejected: {path}")
    if entry_by_path["bin/cosmo-engine"]["mode"] != 0o755:
        _fail("cosmo-engine must be executable in the release payload")
    if entry_by_path[schema.RELEASE_BOOTSTRAP_PATH]["mode"] != 0o755:
        _fail("cosmo-release-bootstrap must be executable in the release payload")
    if entry_by_path[schema.MODEL_PROVISION_PATH]["mode"] != 0o755:
        _fail("cosmo-model-provision must be executable in the release payload")
    bootstrap = payload / schema.RELEASE_BOOTSTRAP_PATH
    _audit_bootstrap_dynamic_contract(bootstrap, readelf)
    if _bootstrap_trust_symbols(bootstrap) != expected_bootstrap_trust:
        _fail("release bootstrap trust anchor differs from the release signing key")
    for path in schema.REQUIRED_RELEASE_SCRIPTS:
        expected_modes = (0o644, 0o755) if path.endswith(".py") else (0o755,)
        if entry_by_path[path]["mode"] not in expected_modes:
            _fail(f"required release script mode rejected: {path}")
    for path in (schema.GUARD_HEADER_PATH,):
        if entry_by_path[path]["mode"] != 0o644:
            _fail(f"release compatibility metadata mode rejected: {path}")
    for directory in schema.FACADE_DIRECTORIES:
        if entry_types.get(directory) != "directory":
            _fail(f"release payload is missing required facade directory: {directory}")
    # Audit the immutable private payload snapshot before the signing key is
    # read. This keeps plaintext presets from ever reaching the
    # release-signing boundary.
    schema._scan_preset_models(payload)
    header_bytes = _checked_file_bytes(
        payload / schema.GUARD_HEADER_PATH,
        schema.MAX_GUARD_HEADER_BYTES,
        "release Model Guard header",
    )
    schema._validate_model_guard_header(header_bytes)
    manifest: dict[str, Any] = {
        "edge": {
            "compatibility_id": "0" * 64,
            "path": "bin/cosmo-engine",
            "sha256": _sha256_file(payload / "bin/cosmo-engine"),
        },
        "device_certificate_schema": 1,
        "format": schema.FORMAT,
        "model_guard": {
            "exports": exports,
            "exports_sha256": exports_sha256,
            "header_path": schema.GUARD_HEADER_PATH,
            "header_sha256": _sha256_file(payload / schema.GUARD_HEADER_PATH),
            "path": f"lib/{schema.GUARD_REAL_FILENAME}",
            "sha256": _sha256_file(payload / f"lib/{schema.GUARD_REAL_FILENAME}"),
        },
        "payload_manifest_sha256": hashlib.sha256(payload_bytes).hexdigest(),
        "release_generation": generation,
        "release_id": release_id,
        "release_key": {"id": key_id, "public_key_sha256": key_sha256},
    }
    manifest["edge"]["compatibility_id"] = schema._compatibility_id(manifest)
    schema._validate_compatibility_manifest(manifest)
    manifest_bytes = schema._canonical_json(manifest)

    _validate_output_before_secret(output)
    signing_key = _read_signing_key_fd(signing_key_fd_inherited)
    with contextlib.ExitStack() as descriptors:
        key_fd = _sealed_memfd(signing_key)
        descriptors.callback(os.close, key_fd)
        message_fd = _readonly_memfd(
            "cosmo-release-manifest", manifest_bytes
        )
        descriptors.callback(os.close, message_fd)
        signature = _run(
            (
                str(openssl),
                "pkeyutl",
                "-sign",
                "-inkey",
                f"/proc/self/fd/{key_fd}",
                "-rawin",
                "-in",
                f"/proc/self/fd/{message_fd}",
            ),
            pass_fds=(key_fd, message_fd),
        )
    if len(signature) != 64:
        _fail("release signer did not produce an Ed25519 signature")
    _verify_ed25519_signature(
        openssl,
        public_key_pem,
        manifest_bytes,
        signature,
        "release manifest",
    )
    _write_bundle(output, manifest_bytes, signature, payload_bytes, entries, sources)
    print(f"Signed release: {output.path}")
    print(f"Release manifest SHA-256: {hashlib.sha256(manifest_bytes).hexdigest()}")

def _snapshot_controlled_file(
    source: Path,
    destination: Path,
    maximum_size: int,
    description: str,
    *,
    executable: bool = False,
) -> Path:
    source_info = source.lstat()
    if executable and not source_info.st_mode & stat.S_IXUSR:
        _fail(f"{description} must be owner-executable")
    data = _checked_file_bytes(source, maximum_size, description)
    destination.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    os.chmod(destination.parent, 0o700)
    mode = 0o500 if executable else 0o400
    descriptor = os.open(
        destination,
        os.O_WRONLY
        | os.O_CREAT
        | os.O_EXCL
        | os.O_CLOEXEC
        | os.O_NOFOLLOW,
        mode,
    )
    try:
        offset = 0
        while offset < len(data):
            offset += os.write(descriptor, data[offset:])
        os.fdatasync(descriptor)
    finally:
        os.close(descriptor)
    return destination


def build_bundle(arguments: argparse.Namespace) -> None:
    signing_key_fd_inherited = _signing_key_fd_is_inherited()
    source_payload = Path(arguments.payload).resolve(strict=True)
    public_key = Path(arguments.release_public_key).resolve(strict=True)
    output = _prepare_controlled_output(arguments.output)
    try:
        with tempfile.TemporaryDirectory(prefix="cosmo-release-payload-snapshot-") as temporary:
            snapshot_parent = Path(temporary)
            os.chmod(snapshot_parent, 0o700)
            snapshot_payload = snapshot_parent / "payload"
            _snapshot_payload(source_payload, snapshot_payload)
            snapshot_controls = snapshot_parent / "release-controls"
            snapshot_public_key = _snapshot_controlled_file(
                public_key,
                snapshot_controls / "release-public-key.pem",
                16 * 1024,
                "release public key",
            )
            _build_bundle_from_snapshot(
                arguments,
                snapshot_payload,
                output,
                snapshot_public_key,
                signing_key_fd_inherited,
            )
    finally:
        os.close(output.directory_fd)


def main(argv: Sequence[str]) -> int:
    try:
        _require_isolated_entrypoint()
    except PackagerError as error:
        print(f"release packaging failed: {error}", file=sys.stderr)
        return 1
    parser = argparse.ArgumentParser(description="Create a signed Cosmo release archive")
    parser.add_argument("--payload", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--release-id", required=True)
    parser.add_argument("--generation", required=True, type=int)
    parser.add_argument("--release-public-key", required=True)
    arguments = parser.parse_args(argv)
    try:
        build_bundle(arguments)
    except (PackagerError, schema.ReleaseError) as error:
        print(f"release packaging failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
