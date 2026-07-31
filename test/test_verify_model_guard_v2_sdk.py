#!/usr/bin/python3
"""Focused tests for the minimal Model Guard SDK check."""

from __future__ import annotations

import contextlib
import importlib.machinery
import importlib.util
import io
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


sys.dont_write_bytecode = True
REPOSITORY = Path(__file__).resolve().parents[1]
VERIFIER_SOURCE = REPOSITORY / "scripts/verify_model_guard_v2_sdk.py"
loader = importlib.machinery.SourceFileLoader(
    "verify_model_guard_v2_sdk_under_test", str(VERIFIER_SOURCE)
)
spec = importlib.util.spec_from_loader(loader.name, loader)
if spec is None:
    raise RuntimeError("cannot load Model Guard SDK verifier")
verifier = importlib.util.module_from_spec(spec)
sys.modules[loader.name] = verifier
loader.exec_module(verifier)


HEADER = (
    b"#define CMG_V2_ABI_MAJOR UINT32_C(2)\n"
    b"#define CMG_V2_ARTIFACT_INFO_SIZE UINT32_C(72)\n"
    b"#define CMG_V2_SOPHON_LOAD_OPTIONS_SIZE UINT32_C(16)\n"
    b"int CmgV2OpenArtifact(void);\n"
    b"int CmgV2GetArtifactInfo(void);\n"
    b"int CmgV2LoadSophonSegment(void);\n"
    b"void CmgV2CloseArtifact(void);\n"
)
LIBRARY = b"synthetic-guard\n"
PROVISION = b"synthetic-provisioner\n"


class ModelGuardHeaderTest(unittest.TestCase):
    def test_required_interface_is_accepted(self) -> None:
        verifier.verify_header(HEADER)

    def test_missing_function_is_rejected(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "CmgV2CloseArtifact"):
            verifier.verify_header(
                HEADER.replace(b"void CmgV2CloseArtifact(void);\n", b"")
            )


class ModelGuardSdkVerifierTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(
            prefix="cosmo-model-guard-sdk-test-"
        )
        self.root = Path(self.temporary.name)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def sdk(
        self,
        name: str,
        *,
        provision: bool = False,
        marked: bool = False,
    ) -> Path:
        root = self.root / f"sdk-{name}"
        for relative in ("include", "lib", "share/cosmo-model-guard"):
            (root / relative).mkdir(parents=True)
        (root / "include/cosmo_model_guard_v2.h").write_bytes(HEADER)
        (root / "lib/libcosmo_model_guard.so.2.0.0").write_bytes(LIBRARY)
        os.symlink(
            "libcosmo_model_guard.so.2.0.0",
            root / "lib/libcosmo_model_guard.so.2",
        )
        os.symlink(
            "libcosmo_model_guard.so.2",
            root / "lib/libcosmo_model_guard.so",
        )
        if provision:
            (root / "bin").mkdir()
            tool = root / "bin/cosmo-model-provision"
            tool.write_bytes(PROVISION)
            tool.chmod(0o755)
        if marked:
            (
                root
                / f"share/cosmo-model-guard/{verifier.TEST_FIXTURE_MARKER_NAME}"
            ).write_bytes(verifier.TEST_FIXTURE_MARKER_CONTENT)
        return root

    def verify(self, sdk: Path, profile: str) -> tuple[str, mock.Mock]:
        arguments = [
            str(VERIFIER_SOURCE),
            "--admission-profile",
            profile,
            "--sdk-root",
            str(sdk),
            "--readelf",
            "/usr/bin/true",
            "--nm",
            "/usr/bin/true",
        ]
        output = io.StringIO()
        with (
            mock.patch.object(sys, "argv", arguments),
            mock.patch.object(verifier, "verify_elf"),
            mock.patch.object(verifier, "verify_provision_tool") as provision_check,
            contextlib.redirect_stdout(output),
        ):
            self.assertEqual(verifier.main(), 0)
        return output.getvalue(), provision_check

    def test_public_runtime_accepts_header_and_library(self) -> None:
        output, provision_check = self.verify(
            self.sdk("public"), verifier.ADMISSION_PUBLIC_RUNTIME
        )

        self.assertIn("sdk_profile=public-runtime", output)
        self.assertIn("library_sha256=", output)
        provision_check.assert_not_called()

    def test_production_profile_adds_provisioning_tool(self) -> None:
        output, provision_check = self.verify(
            self.sdk("production", provision=True),
            verifier.ADMISSION_PRODUCTION_RELEASE,
        )

        self.assertIn("sdk_profile=production", output)
        provision_check.assert_called_once()

    def test_production_profile_requires_provisioning_tool(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "cannot read SDK file"):
            self.verify(
                self.sdk("missing-provision"),
                verifier.ADMISSION_PRODUCTION_RELEASE,
            )

    def test_fixture_requires_marker(self) -> None:
        sdk = self.sdk("fixture", provision=True, marked=True)

        output, _ = self.verify(sdk, verifier.ADMISSION_TEST_FIXTURE)
        self.assertIn("sdk_profile=TEST-FIXTURE-DO-NOT-DEPLOY", output)

        (
            sdk
            / f"share/cosmo-model-guard/{verifier.TEST_FIXTURE_MARKER_NAME}"
        ).unlink()
        with self.assertRaisesRegex(RuntimeError, "requires the exact"):
            self.verify(sdk, verifier.ADMISSION_TEST_FIXTURE)

    def test_file_permissions_and_header_symlink_are_not_inputs(self) -> None:
        sdk = self.sdk("permissive")
        header = sdk / "include/cosmo_model_guard_v2.h"
        target = header.with_name("header-target.h")
        header.rename(target)
        os.symlink(target.name, header)
        for path in (sdk, sdk / "include", sdk / "lib"):
            path.chmod(0o777)
        target.chmod(0o666)
        (sdk / "lib/libcosmo_model_guard.so.2.0.0").chmod(0o777)

        output, _ = self.verify(sdk, verifier.ADMISSION_PUBLIC_RUNTIME)
        self.assertIn("sdk_profile=public-runtime", output)


if __name__ == "__main__":
    unittest.main()
