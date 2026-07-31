#!/usr/bin/python3
"""Generate the AArch64 release-verifier trust-anchor object.

The public key is not secret, but production must inject it as a generated ELF
object rather than accepting a runtime configuration file.  The main CMake
integration must require this object and fail configuration/linking when it is
absent.  This generator never supplies a default or test key.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import stat
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Sequence


OPENSSL = Path("/usr/bin/openssl")
ASSEMBLER = Path("/usr/bin/aarch64-linux-gnu-as")
READELF = Path("/usr/bin/aarch64-linux-gnu-readelf")


class GenerationError(RuntimeError):
    pass


def _require_isolated_entrypoint() -> None:
    if sys.flags.isolated != 1:
        raise GenerationError(
            "release public-key object generation must be launched with "
            "/usr/bin/python3 -I -B"
        )


def _run(arguments: Sequence[str]) -> bytes:
    try:
        result = subprocess.run(
            list(arguments),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            close_fds=True,
            env={"LC_ALL": "C", "PATH": "/usr/bin:/bin"},
            timeout=30,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise GenerationError(f"required tool failed: {arguments[0]}") from error
    if result.returncode != 0:
        raise GenerationError(f"required tool rejected input: {Path(arguments[0]).name}")
    return result.stdout


def _validate_tool(path: Path) -> None:
    info = os.stat(path)
    if not stat.S_ISREG(info.st_mode):
        raise GenerationError(f"tool is not a regular file: {path}")


def _assembly_bytes(label: str, data: bytes) -> str:
    values = ",".join(f"0x{value:02x}" for value in data)
    return (
        f".hidden {label}\n"
        f".global {label}\n"
        f".type {label}, %object\n"
        f".size {label}, {len(data)}\n"
        f"{label}:\n"
        f".byte {values}\n"
    )


def generate(public_key: Path, output: Path) -> None:
    for tool in (OPENSSL, ASSEMBLER, READELF):
        _validate_tool(tool)
    key_info = os.stat(public_key)
    if not stat.S_ISREG(key_info.st_mode):
        raise GenerationError("release public key is not a regular file")
    pem = public_key.read_bytes()
    if not pem or len(pem) > 16 * 1024:
        raise GenerationError("release public key size rejected")
    canonical_pem = _run((str(OPENSSL), "pkey", "-pubin", "-in", str(public_key), "-pubout"))
    if pem != canonical_pem:
        raise GenerationError("release public key PEM is not in the canonical OpenSSL encoding")
    der = _run((str(OPENSSL), "pkey", "-pubin", "-in", str(public_key), "-outform", "DER"))
    prefix = bytes.fromhex("302a300506032b6570032100")
    if len(der) != 44 or der[: len(prefix)] != prefix:
        raise GenerationError("release public key must be Ed25519")
    raw_key = der[-32:]
    key_id = hashlib.sha256(
        b"cosmo-release-key-id-v1" + (1).to_bytes(2, "big") + raw_key
    ).digest()[:16]
    pem_digest = hashlib.sha256(pem).digest()
    if not any(key_id):
        raise GenerationError("derived release key ID is zero")

    output_parent = output.parent.resolve(strict=True)
    parent_info = os.stat(output_parent)
    if not stat.S_ISDIR(parent_info.st_mode):
        raise GenerationError("output parent must be a directory")
    if output.exists() or output.is_symlink():
        raise GenerationError("release public-key object output already exists")

    source = (
        '.section .rodata.cosmo_release_key,"a",%progbits\n'
        ".balign 16\n"
        + _assembly_bytes("cosmo_release_public_key_raw_v1", raw_key)
        + ".balign 16\n"
        + _assembly_bytes("cosmo_release_public_key_id_v1", key_id)
        + ".balign 16\n"
        + _assembly_bytes("cosmo_release_public_key_pem_sha256_v1", pem_digest)
        + '.section .note.GNU-stack,"",%progbits\n'
    )

    with tempfile.TemporaryDirectory(prefix="cosmo-release-key-object-", dir=output_parent) as temporary:
        root = Path(temporary)
        source_path = root / "release-key.s"
        object_path = root / "release-key.o"
        source_fd = os.open(
            source_path,
            os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC | os.O_NOFOLLOW,
            0o600,
        )
        try:
            os.write(source_fd, source.encode("ascii"))
            os.fdatasync(source_fd)
        finally:
            os.close(source_fd)
        _run((str(ASSEMBLER), "-o", str(object_path), str(source_path)))
        header = _run((str(READELF), "-hW", str(object_path))).decode("utf-8", "replace")
        if (
            re.search(r"^\s*Type:\s+REL \(Relocatable file\)\s*$", header, re.MULTILINE) is None
            or re.search(r"^\s*Machine:\s+AArch64\s*$", header, re.MULTILINE) is None
        ):
            raise GenerationError("generated trust-anchor object is not AArch64 relocatable ELF")
        object_info = os.lstat(object_path)
        if not stat.S_ISREG(object_info.st_mode):
            raise GenerationError("generated trust-anchor object type rejected")
        destination_fd = os.open(
            output,
            os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC | os.O_NOFOLLOW,
            0o600,
        )
        try:
            with object_path.open("rb") as source_stream:
                while True:
                    block = source_stream.read(65536)
                    if not block:
                        break
                    offset = 0
                    while offset < len(block):
                        offset += os.write(destination_fd, block[offset:])
            os.fdatasync(destination_fd)
        except BaseException:
            try:
                os.unlink(output)
            except OSError:
                pass
            raise
        finally:
            os.close(destination_fd)
    directory_fd = os.open(output_parent, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
    try:
        os.fsync(directory_fd)
    finally:
        os.close(directory_fd)


def main(argv: Sequence[str]) -> int:
    try:
        _require_isolated_entrypoint()
    except GenerationError as error:
        print(f"release public-key object generation failed: {error}", file=sys.stderr)
        return 1
    parser = argparse.ArgumentParser(description="Generate the release trust-anchor AArch64 object")
    parser.add_argument("--public-key", required=True)
    parser.add_argument("--output", required=True)
    arguments = parser.parse_args(argv)
    try:
        generate(Path(arguments.public_key).resolve(strict=True), Path(arguments.output).resolve(strict=False))
    except (GenerationError, OSError) as error:
        print(f"release public-key object generation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
