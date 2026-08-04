#!/usr/bin/python3
"""Regression tests for Open/Protected permanent MD5 package policy."""

from __future__ import annotations

import hashlib
import importlib.util
import io
import pathlib
import tarfile
import tempfile
import unittest


REPOSITORY = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "package_verifier", REPOSITORY / "scripts/verify_package_contents.py"
)
assert spec and spec.loader
verifier = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verifier)


class PackageProfileTests(unittest.TestCase):
    def make_package(self, profile: str, model: bytes = b"plain-model") -> pathlib.Path:
        root = "cosmo-V1.5.0"
        directory = pathlib.Path(tempfile.mkdtemp())
        initial = directory / f"{root}.tar.gz"
        executable_files = verifier.REQUIRED_EXECUTABLES
        regular_files = verifier.REQUIRED_FILES
        with tarfile.open(initial, "w:gz") as archive:
            root_info = tarfile.TarInfo(root)
            root_info.type = tarfile.DIRTYPE
            root_info.mode = 0o755
            archive.addfile(root_info)
            for name in sorted(verifier.REQUIRED_DIRS):
                info = tarfile.TarInfo(f"{root}/{name}")
                info.type = tarfile.DIRTYPE
                info.mode = 0o755
                archive.addfile(info)
            files = set(executable_files) | set(regular_files)
            if profile == "production-release":
                files.add("bin/cosmo-model-provision")
            for name in sorted(files):
                data = b"#!/bin/sh\n" if name in executable_files or name.endswith("provision") else b"V1.5.0\n"
                info = tarfile.TarInfo(f"{root}/{name}")
                info.size = len(data)
                info.mode = 0o755 if name in executable_files or name.endswith("provision") else 0o644
                archive.addfile(info, io.BytesIO(data))
            model_path = f"{root}/resource/models/preset/model.nn"
            info = tarfile.TarInfo(model_path)
            info.size = len(model)
            info.mode = 0o644
            archive.addfile(info, io.BytesIO(model))
        digest = hashlib.md5(initial.read_bytes(), usedforsecurity=False).hexdigest()
        final = directory / f"{root}-{digest}.tar.gz"
        initial.rename(final)
        return final

    def test_open_accepts_plain_model(self) -> None:
        verifier.verify_package(self.make_package("public-runtime"), "public-runtime")

    def test_protected_accepts_encrypted_model(self) -> None:
        verifier.verify_package(
            self.make_package("production-release", b"CEMC" + b"encrypted"),
            "production-release",
        )

    def test_channels_reject_each_others_model_format(self) -> None:
        with self.assertRaises(verifier.PackageAuditError):
            verifier.verify_package(
                self.make_package("public-runtime", b"CEMCencrypted"), "public-runtime"
            )
        with self.assertRaises(verifier.PackageAuditError):
            verifier.verify_package(
                self.make_package("production-release", b"plain"), "production-release"
            )

    def test_build_has_no_signed_release_switches(self) -> None:
        build_inputs = (
            (REPOSITORY / "CMakeLists.txt").read_text(encoding="utf-8")
            + (REPOSITORY / "scripts/build.sh").read_text(encoding="utf-8")
            + (REPOSITORY / "docker-compose.sophon.yml").read_text(encoding="utf-8")
        )
        for obsolete in (
            "COSMO_RELEASE_PUBLIC_KEY_OBJECT",
            "COSMO_REQUIRE_RELEASE_BOOTSTRAP",
            "COSMO_LEGACY_MIGRATION_PACKAGE",
            "cosmo-release-bootstrap",
        ):
            self.assertNotIn(obsolete, build_inputs)


if __name__ == "__main__":
    unittest.main()
