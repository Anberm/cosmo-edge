#!/usr/bin/python3
"""Strictly verify a production AArch64 release trust-anchor object."""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import stat
import subprocess
import sys
from pathlib import Path
from typing import Sequence


EXPECTED_SYMBOLS = {
    "cosmo_release_public_key_raw_v1": (32, 0),
    "cosmo_release_public_key_id_v1": (16, 32),
    "cosmo_release_public_key_pem_sha256_v1": (32, 48),
}


class VerificationError(RuntimeError):
    pass


def _require_isolated_entrypoint() -> None:
    if sys.flags.isolated != 1:
        raise VerificationError(
            "release public-key object verification must be launched with "
            "/usr/bin/python3 -I -B"
        )


def _run(arguments: Sequence[str]) -> str:
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
        raise VerificationError("required target inspection tool failed") from error
    if result.returncode != 0:
        raise VerificationError("target inspection tool rejected the release key object")
    return result.stdout.decode("utf-8", "strict")


def _validate_tool(path: Path) -> None:
    info = os.stat(path)
    if not path.is_absolute() or not stat.S_ISREG(info.st_mode):
        raise VerificationError(f"inspection tool is not a regular file: {path}")


def verify(path: Path, readelf: Path, nm: Path) -> None:
    if not path.is_absolute() or not readelf.is_absolute() or not nm.is_absolute():
        raise VerificationError("release key object and inspection tools must use absolute paths")
    _validate_tool(readelf)
    _validate_tool(nm)
    info = os.stat(path)
    if (
        not stat.S_ISREG(info.st_mode)
        or info.st_size <= 0
    ):
        raise VerificationError("release key object type or size rejected")
    header = _run((str(readelf), "-hW", str(path)))
    if (
        re.search(r"^\s*Type:\s+REL \(Relocatable file\)\s*$", header, re.MULTILINE) is None
        or re.search(r"^\s*Machine:\s+AArch64\s*$", header, re.MULTILINE) is None
    ):
        raise VerificationError("release key object must be an AArch64 relocatable ELF")
    sections = _run((str(readelf), "-SW", str(path)))
    matching_sections = [line for line in sections.splitlines() if ".rodata.cosmo_release_key" in line]
    section = (
        re.search(
            r"^\s*\[\s*(\d+)\]\s+\.rodata\.cosmo_release_key\s+PROGBITS\s+"
            r"([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+([0-9a-fA-F]+)\s+"
            r"[0-9a-fA-F]+\s+([A-Z]+)\s+\d+\s+\d+\s+\d+\s*$",
            matching_sections[0],
        )
        if len(matching_sections) == 1
        else None
    )
    if (
        section is None
        or int(section.group(2), 16) != 0
        or int(section.group(3), 16) != 80
        or section.group(4) != "A"
    ):
        raise VerificationError("release key object trust section size/flags rejected")
    trust_section_index = int(section.group(1))
    for line in sections.splitlines():
        executable = re.search(
            r"^\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+[0-9a-fA-F]+\s+"
            r"[0-9a-fA-F]+\s+([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+([A-Z]+)",
            line,
        )
        if (
            executable is not None
            and int(executable.group(2), 16) != 0
            and "X" in executable.group(3)
        ):
            raise VerificationError("release key object contains executable content")
        if (
            executable is not None
            and int(executable.group(2), 16) != 0
            and "A" in executable.group(3)
            and executable.group(1) != ".rodata.cosmo_release_key"
        ):
            raise VerificationError("release key object contains unexpected allocated content")

    symbols = _run((str(readelf), "-sW", str(path)))
    found: dict[str, tuple[int, int]] = {}
    global_defined: set[str] = set()
    pattern = re.compile(
        r"^\s*\d+:\s+([0-9a-fA-F]+)\s+(\d+)\s+(\S+)\s+"
        r"(GLOBAL|WEAK)\s+(\S+)\s+(\S+)\s+(\S+)\s*$"
    )
    for line in symbols.splitlines():
        match = pattern.match(line)
        if match is None:
            continue
        name = match.group(7)
        section_index = match.group(6)
        if section_index == "UND":
            continue
        global_defined.add(name)
        if (
            name not in EXPECTED_SYMBOLS
            or match.group(3) != "OBJECT"
            or match.group(4) != "GLOBAL"
            or match.group(5) != "HIDDEN"
            or section_index != str(trust_section_index)
        ):
            raise VerificationError("release key object contains an unexpected defined symbol")
        if name in found:
            raise VerificationError("release key object contains a duplicate trust symbol")
        found[name] = (int(match.group(2)), int(match.group(1), 16))
    if found != EXPECTED_SYMBOLS or global_defined != set(EXPECTED_SYMBOLS):
        raise VerificationError("release key object symbol whitelist, size, or offset rejected")

    undefined = _run((str(nm), "-u", str(path))).strip()
    if undefined:
        raise VerificationError("release key object must not contain undefined symbols")
    relocations = _run((str(readelf), "-rW", str(path)))
    if "There are no relocations in this file." not in relocations:
        raise VerificationError("release key object must not contain relocations")


def main(argv: Sequence[str]) -> int:
    try:
        _require_isolated_entrypoint()
    except VerificationError as error:
        print(f"release public-key object verification failed: {error}", file=sys.stderr)
        return 1
    parser = argparse.ArgumentParser(description="Verify the production release key object")
    parser.add_argument("--object", required=True)
    parser.add_argument("--readelf", required=True)
    parser.add_argument("--nm", required=True)
    arguments = parser.parse_args(argv)
    try:
        verify(
            Path(arguments.object),
            Path(arguments.readelf),
            Path(arguments.nm),
        )
    except (OSError, UnicodeError, VerificationError) as error:
        print(f"release public-key object verification failed: {error}", file=sys.stderr)
        return 1
    digest = hashlib.sha256(Path(arguments.object).read_bytes()).hexdigest()
    print("release_public_key_object=aarch64-relocatable-v1")
    print(f"release_public_key_object_sha256={digest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
