#!/usr/bin/python3
"""Private backend for the stable embedded-key factory bootstrap.

The script is loaded from an already-open descriptor by the C++ verifier. It
has exactly two internal modes and is not a public updater CLI.
"""

from __future__ import annotations

import errno
import gzip
import importlib.machinery
import importlib.util
import os
import socket
import stat
import struct
import sys
from pathlib import Path
from typing import NoReturn


sys.dont_write_bytecode = True
INSTALL_ROOT = Path("/appfs/cosmo_wander/cwai_data")
PERSISTENT_ROOT = Path("/data/cwaiuserdata/model-guard")
STABLE_ROOT = INSTALL_ROOT / ".release-bootstrap"
ARCHIVE_FD = 3
CHANNEL_FD = 4
BACKEND_SOURCE_FD = 5
UPDATER_SOURCE_FD = 6
BACKEND_SOURCE_PATH = f"/proc/self/fd/{BACKEND_SOURCE_FD}"
UPDATER_SOURCE_PATH = f"/proc/self/fd/{UPDATER_SOURCE_FD}"
INSTALL_MODE = "--install"
RECOVER_MODE = "--recover"
REQUEST_MAGIC = 0x43425231  # CBR1
APPROVAL_MAGIC = 0x43424131  # CBA1
MAXIMUM_MANIFEST = 128 * 1024
MAXIMUM_PEM = 16 * 1024
MAXIMUM_SCRIPT = 8 * 1024 * 1024
MAXIMUM_CHANNEL_ALLOCATION = MAXIMUM_MANIFEST + 1024
MAXIMUM_ARCHIVE = 128 * 1024 * 1024 * 1024
CANONICAL_GZIP_HEADER = b"\x1f\x8b\x08\x00\x00\x00\x00\x00\x02\xff"


class BackendError(RuntimeError):
    pass


def _fail(message: str) -> NoReturn:
    raise BackendError(message)


def _regular_info(
    info: os.stat_result,
    *,
    executable: bool,
    maximum_size: int,
) -> bool:
    return (
        stat.S_ISREG(info.st_mode)
        and info.st_size > 0
        and info.st_size <= maximum_size
        and (not executable or bool(info.st_mode & 0o111))
    )


def _validate_descriptor(
    descriptor: int,
    *,
    executable: bool,
    maximum_size: int,
) -> None:
    descriptor_info = os.fstat(descriptor)
    if not _regular_info(
        descriptor_info,
        executable=executable,
        maximum_size=maximum_size,
    ):
        _fail("bootstrap component descriptor rejected")


def _read_exact(channel: socket.socket, size: int) -> bytes:
    if not isinstance(size, int) or size < 0 or size > MAXIMUM_CHANNEL_ALLOCATION:
        _fail("embedded verifier requested an oversized channel allocation")
    output = bytearray(size)
    view = memoryview(output)
    offset = 0
    while offset < size:
        count = channel.recv_into(view[offset:], size - offset)
        if count <= 0:
            _fail("embedded verifier closed its channel")
        offset += count
    return bytes(output)


def _send_all(channel: socket.socket, data: bytes) -> None:
    if len(data) > MAXIMUM_CHANNEL_ALLOCATION:
        _fail("bootstrap metadata exceeds its verifier channel limit")
    view = memoryview(data)
    while view:
        count = channel.send(view)
        if count <= 0:
            _fail("cannot send metadata to the embedded verifier")
        view = view[count:]


def _authenticate_parent(channel: socket.socket, operation: str) -> None:
    expected_arguments = [BACKEND_SOURCE_PATH, operation]
    if (
        os.getuid() != 0
        or os.geteuid() != 0
        or os.getgid() != 0
        or os.getegid() != 0
        or sys.argv != expected_arguments
    ):
        _fail("bootstrap backend invocation rejected")
    if channel.getsockopt(socket.SOL_SOCKET, socket.SO_TYPE) != socket.SOCK_STREAM:
        _fail("bootstrap backend channel type rejected")

    parent = os.getppid()
    if parent <= 1:
        _fail("bootstrap backend parent is unavailable")
    credential_size = struct.calcsize("3i")
    credentials = channel.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, credential_size)
    peer_pid, peer_uid, peer_gid = struct.unpack("3i", credentials)
    if peer_pid != parent or peer_uid != 0 or peer_gid != 0:
        _fail("bootstrap backend peer credentials rejected")

    _validate_descriptor(
        BACKEND_SOURCE_FD,
        executable=False,
        maximum_size=MAXIMUM_SCRIPT,
    )
    _validate_descriptor(
        UPDATER_SOURCE_FD,
        executable=False,
        maximum_size=MAXIMUM_SCRIPT,
    )


def _require_recovery_descriptor_layout() -> None:
    try:
        os.fstat(ARCHIVE_FD)
    except OSError as error:
        if error.errno == errno.EBADF:
            return
        raise
    _fail("bootstrap recovery inherited an unexpected archive descriptor")


def _read_stream_exact(stream, size: int) -> bytes:
    if size < 0 or size > MAXIMUM_CHANNEL_ALLOCATION:
        _fail("bootstrap archive requested an oversized parser allocation")
    output = bytearray(size)
    view = memoryview(output)
    offset = 0
    while offset < size:
        count = stream.readinto(view[offset:])
        if count is None or count <= 0:
            _fail("bootstrap archive ended before signed metadata")
        offset += count
    return bytes(output)


def _tar_string(field: bytes, description: str) -> str:
    terminator = field.find(b"\0")
    if terminator < 0 or any(field[terminator + 1 :]):
        _fail(f"bootstrap archive {description} is not canonically terminated")
    try:
        return field[:terminator].decode("ascii", "strict")
    except UnicodeError as error:
        raise BackendError(f"bootstrap archive {description} is not ASCII") from error


def _tar_octal(field: bytes, description: str, maximum: int) -> int:
    value = field.rstrip(b"\0 ").lstrip(b" ")
    if not value or any(byte < ord("0") or byte > ord("7") for byte in value):
        _fail(f"bootstrap archive {description} is not canonical octal")
    parsed = int(value, 8)
    if parsed > maximum:
        _fail(f"bootstrap archive {description} exceeds its limit")
    return parsed


def _read_canonical_tar_header(stream) -> tuple[str, bytes, int, int]:
    header = _read_stream_exact(stream, 512)
    if header == bytes(512):
        _fail("bootstrap archive ended before signed metadata")
    stored_checksum = _tar_octal(header[148:156], "header checksum", 255 * 512)
    checksum_header = header[:148] + b" " * 8 + header[156:]
    if sum(checksum_header) != stored_checksum:
        _fail("bootstrap archive header checksum rejected")
    if header[257:263] != b"ustar\0" or header[263:265] != b"00" or any(header[345:500]):
        _fail("bootstrap archive signed metadata must use canonical ustar headers")
    canonical_owner = b"root" + bytes(28)
    if (
        any(header[157:257])
        or header[265:329] != canonical_owner + canonical_owner
        or any(header[329:345])
        or any(header[500:512])
    ):
        _fail("bootstrap archive signed metadata header contains unexpected fields")
    name = _tar_string(header[0:100], "member name")
    mode = _tar_octal(header[100:108], "member mode", 0o7777)
    if _tar_octal(header[108:116], "member uid", 0) != 0:
        _fail("bootstrap archive signed metadata uid rejected")
    if _tar_octal(header[116:124], "member gid", 0) != 0:
        _fail("bootstrap archive signed metadata gid rejected")
    size = _tar_octal(header[124:136], "member size", MAXIMUM_MANIFEST)
    if _tar_octal(header[136:148], "member mtime", 0) != 0:
        _fail("bootstrap archive signed metadata mtime rejected")
    return name, header[156:157], size, mode


def _read_canonical_tar_member(stream, expected_name: str, maximum: int) -> bytes:
    name, object_type, size, mode = _read_canonical_tar_header(stream)
    if name != expected_name or object_type != b"0" or mode != 0o600 or size <= 0 or size > maximum:
        _fail("bootstrap archive signed metadata order or type rejected")
    value = _read_stream_exact(stream, size)
    padding_size = (-size) % 512
    if padding_size and any(_read_stream_exact(stream, padding_size)):
        _fail("bootstrap archive signed metadata padding rejected")
    return value


def _read_signed_metadata(archive_fd: int) -> tuple[bytes, bytes]:
    info = os.fstat(archive_fd)
    if (
        not stat.S_ISREG(info.st_mode)
        or info.st_size <= 0
        or info.st_size > MAXIMUM_ARCHIVE
    ):
        _fail("bootstrap archive FD type or size rejected")
    os.lseek(archive_fd, 0, os.SEEK_SET)
    try:
        with os.fdopen(os.dup(archive_fd), "rb") as raw:
            # The offline builder fixes filename="", mtime=0, level=9 and no
            # optional gzip fields.  Requiring that header before handing the
            # stream to gzip prevents attacker-sized FNAME/FCOMMENT parsing.
            if _read_stream_exact(raw, len(CANONICAL_GZIP_HEADER)) != CANONICAL_GZIP_HEADER:
                _fail("bootstrap archive gzip header is not canonical")
            raw.seek(0)
            with gzip.GzipFile(fileobj=raw, mode="rb") as bundle:
                name, object_type, size, mode = _read_canonical_tar_header(bundle)
                if name != "meta/" or object_type != b"5" or size != 0 or mode != 0o755:
                    _fail("bootstrap archive canonical metadata directory is missing")
                manifest = _read_canonical_tar_member(
                    bundle,
                    "meta/compatibility.manifest.json",
                    MAXIMUM_MANIFEST,
                )
                signature = _read_canonical_tar_member(
                    bundle,
                    "meta/compatibility.manifest.sig",
                    64,
                )
    except (EOFError, gzip.BadGzipFile, OSError) as error:
        raise BackendError("bootstrap archive metadata parsing failed") from error
    if len(signature) != 64:
        _fail("bootstrap archive signed metadata is incomplete")
    os.lseek(archive_fd, 0, os.SEEK_SET)
    return manifest, signature


def _load_updater():
    _validate_descriptor(
        UPDATER_SOURCE_FD,
        executable=False,
        maximum_size=MAXIMUM_SCRIPT,
    )
    loader = importlib.machinery.SourceFileLoader(
        "cosmo_release_updater",
        UPDATER_SOURCE_PATH,
    )
    specification = importlib.util.spec_from_loader(loader.name, loader)
    if specification is None:
        _fail("cannot load the trusted release transaction implementation")
    module = importlib.util.module_from_spec(specification)
    sys.modules[loader.name] = module
    loader.exec_module(module)
    return module


def _validate_committed_release(installed: object) -> Path:
    if not isinstance(installed, Path):
        _fail("bootstrap transaction returned a non-path result")
    expected_parent = INSTALL_ROOT / ".releases"
    if not installed.is_absolute() or installed.parent != expected_parent or not installed.name:
        _fail("bootstrap transaction returned an unexpected release path")
    return installed


def _new_updater(release_module):
    paths = release_module.ReleasePaths(
        install_root=INSTALL_ROOT,
        model_guard_state_root=PERSISTENT_ROOT,
    )
    return release_module.ReleaseUpdater(paths)


def _install(channel: socket.socket) -> int:
    manifest, signature = _read_signed_metadata(ARCHIVE_FD)
    request = struct.pack("!III", REQUEST_MAGIC, len(manifest), len(signature))
    _send_all(channel, request + manifest + signature)

    response = _read_exact(channel, 8)
    magic, pem_size = struct.unpack("!II", response)
    if magic != APPROVAL_MAGIC or pem_size <= 0 or pem_size > MAXIMUM_PEM:
        _fail("embedded verifier did not approve the release manifest")
    raw_key = _read_exact(channel, 32)
    key_id = _read_exact(channel, 16)
    pem_sha256 = _read_exact(channel, 32)
    public_key = _read_exact(channel, pem_size)
    if channel.recv(1):
        _fail("embedded verifier protocol has trailing data")
    channel.close()

    release = _load_updater()
    updater = _new_updater(release)
    try:
        installed = updater.bootstrap_from_embedded_verifier(
            ARCHIVE_FD,
            manifest,
            signature,
            public_key,
            raw_key,
            key_id,
            pem_sha256,
        )
    except BaseException:
        recovered = None
        try:
            recovered = updater.recover_failed_bootstrap()
        except BaseException:
            pass
        if recovered is None:
            raise
        installed = recovered
    print(_validate_committed_release(installed))
    return 0


def _recover(channel: socket.socket) -> int:
    _require_recovery_descriptor_layout()
    channel.close()
    release = _load_updater()
    updater = _new_updater(release)
    had_pending_bootstrap = updater.paths.bootstrap_journal_file.exists()
    recovered = updater.recover_failed_bootstrap()
    print(_recovery_result(had_pending_bootstrap, recovered))
    return 0


def _recovery_result(had_pending_bootstrap: bool, recovered: Path | None) -> str:
    """Render the two successful, explicit bootstrap recovery outcomes."""
    if recovered is not None:
        return str(_validate_committed_release(recovered))
    if had_pending_bootstrap:
        return "legacy-restored"
    _fail("factory bootstrap recovery did not find a pending transaction")


def main() -> int:
    if sys.argv == [BACKEND_SOURCE_PATH, INSTALL_MODE]:
        operation = INSTALL_MODE
    elif sys.argv == [BACKEND_SOURCE_PATH, RECOVER_MODE]:
        operation = RECOVER_MODE
    else:
        _fail("bootstrap backend accepts only exact --install or --recover invocation")

    channel = socket.socket(fileno=CHANNEL_FD)
    _authenticate_parent(channel, operation)
    if operation == INSTALL_MODE:
        return _install(channel)
    return _recover(channel)


if __name__ == "__main__":
    try:
        if sys.flags.isolated != 1:
            _fail(
                "release bootstrap backend must be launched with "
                "/usr/bin/python3 -I -B"
            )
        raise SystemExit(main())
    except (BackendError, OSError) as error:
        print(f"release bootstrap rejected operation: {error}", file=sys.stderr)
        raise SystemExit(1)
    except Exception:
        print("release bootstrap rejected operation: trusted transaction failed", file=sys.stderr)
        raise SystemExit(1)
