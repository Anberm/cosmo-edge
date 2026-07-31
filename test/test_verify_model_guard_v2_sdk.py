#!/usr/bin/python3
"""Focused tests for the minimal Model Guard SDK compatibility check."""

from __future__ import annotations

import contextlib
import hashlib
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


HEADER = b"#define CMG_V2_ABI_MAJOR 2\n"
LIBRARY = b"synthetic-aarch64-guard\n"
ABI = b'{"schema":"test-abi"}\n'
DEPENDENCIES = b'{"schema":"test-dependencies"}\n'
PROVISION = b"synthetic-aarch64-provisioner\n"


def sdk_release_manifest(
    *,
    library: bytes = LIBRARY,
    header: bytes = HEADER,
    abi: bytes = ABI,
    dependencies: bytes = DEPENDENCIES,
) -> bytes:
    values = {
        "CMG_SDK_ABI_SHA256": hashlib.sha256(abi).hexdigest(),
        "CMG_SDK_DEPENDENCIES_SHA256": hashlib.sha256(dependencies).hexdigest(),
        "CMG_SDK_HEADER_SHA256": hashlib.sha256(header).hexdigest(),
        "CMG_SDK_LIBRARY_SHA256": hashlib.sha256(library).hexdigest(),
        "CMG_SDK_RELEASE_FORMAT": "cosmo-model-guard-sdk-release-v2",
        "CMG_SDK_RELEASE_ID": "cmg-sdk-v2.3.3",
    }
    return "".join(
        f"{key}={values[key]}\n" for key in verifier.SDK_RELEASE_KEYS
    ).encode("ascii")


class SdkReleaseManifestTest(unittest.TestCase):
    def verify(self, manifest: bytes, *, header: bytes = HEADER) -> dict[str, str]:
        return verifier.verify_sdk_release_manifest(
            manifest,
            library=LIBRARY,
            header=header,
            abi_manifest=ABI,
            dependency_manifest=DEPENDENCIES,
            expected_profile=verifier.PROFILE_V2_ONLY,
        )

    def test_six_field_manifest_is_admitted(self) -> None:
        values = self.verify(sdk_release_manifest())

        self.assertEqual(tuple(values), verifier.SDK_RELEASE_KEYS)
        self.assertEqual(len(values), 6)
        self.assertEqual(values["CMG_SDK_RELEASE_ID"], "cmg-sdk-v2.3.3")

    def test_removed_gate_field_is_rejected(self) -> None:
        manifest = (
            sdk_release_manifest()
            + b"CMG_SDK_RELEASE_STATE=FINAL\n"
        )

        with self.assertRaisesRegex(RuntimeError, "keys or canonical order"):
            verifier.parse_sdk_release_manifest(manifest)

    def test_component_change_is_rejected(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "SHA-256 mismatch: header"):
            self.verify(sdk_release_manifest(), header=b"changed\n")


class ModelGuardSdkVerifierTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(
            prefix="cosmo-model-guard-sdk-test-"
        )
        self.root = Path(self.temporary.name)
        self.snapshot_base = self.root / "snapshots"
        self.openssl_include = self.root / "openssl/include"
        self.sophon_include = self.root / "sophon/include"
        self.sophon_lib = self.root / "sophon/lib"
        (self.openssl_include / "openssl").mkdir(parents=True)
        self.sophon_include.mkdir(parents=True)
        self.sophon_lib.mkdir(parents=True)
        (self.openssl_include / "openssl/opensslv.h").write_bytes(b"openssl\n")
        (self.sophon_include / "bmlib_runtime.h").write_bytes(b"sophon\n")
        (self.sophon_lib / "libbmrt.so.1.0").write_bytes(b"bmrt\n")
        os.symlink("libbmrt.so.1.0", self.sophon_lib / "libbmrt.so")
        self.libcrypto = self.root / "openssl/lib/libcrypto.so.3"
        self.libcrypto.parent.mkdir(parents=True)
        self.libcrypto.write_bytes(b"crypto\n")

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def sdk(
        self,
        name: str,
        *,
        provision: bool = False,
        marked: bool = False,
        manifest: bool = True,
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
        (root / "share/cosmo-model-guard/cmg_v2_abi.json").write_bytes(ABI)
        (
            root / "share/cosmo-model-guard/cmg_v2_dependencies.json"
        ).write_bytes(DEPENDENCIES)
        if manifest:
            (
                root
                / f"share/cosmo-model-guard/{verifier.SDK_RELEASE_MANIFEST_NAME}"
            ).write_bytes(sdk_release_manifest())
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

    def verify(
        self,
        sdk: Path,
        profile: str,
        *,
        expected_snapshot: Path | None = None,
    ) -> tuple[str, mock.Mock]:
        arguments = [
            str(VERIFIER_SOURCE),
            "--admission-profile",
            profile,
            "--sdk-root",
            str(sdk),
            "--snapshot-base",
            str(self.snapshot_base),
            "--readelf",
            "/usr/bin/true",
            "--nm",
            "/usr/bin/true",
            "--openssl-include-dir",
            str(self.openssl_include),
            "--libcrypto",
            str(self.libcrypto),
            "--sophon-include-dir",
            str(self.sophon_include),
            "--sophon-library-dir",
            str(self.sophon_lib),
        ]
        if expected_snapshot is not None:
            arguments.extend(("--expected-snapshot-root", str(expected_snapshot)))
        output = io.StringIO()
        with (
            mock.patch.object(sys, "argv", arguments),
            mock.patch.object(verifier, "verify_header"),
            mock.patch.object(verifier, "verify_manifest"),
            mock.patch.object(verifier, "verify_dependencies"),
            mock.patch.object(verifier, "verify_elf"),
            mock.patch.object(verifier, "verify_provision_tool") as provision_check,
            contextlib.redirect_stdout(output),
        ):
            self.assertEqual(verifier.main(), 0)
        return output.getvalue(), provision_check

    @staticmethod
    def verified_root(output: str) -> Path:
        return Path(
            next(
                line.partition("=")[2]
                for line in output.splitlines()
                if line.startswith("verified_sdk_root=")
            )
        )

    def test_public_runtime_accepts_runtime_components_only(self) -> None:
        output, provision_check = self.verify(
            self.sdk("public"), verifier.ADMISSION_PUBLIC_RUNTIME
        )

        self.assertIn("sdk_release_id=cmg-sdk-v2.3.3", output)
        self.assertIn("sdk_profile=public-runtime", output)
        self.assertNotIn("trust", output)
        provision_check.assert_not_called()

    def test_production_profile_only_adds_provisioning_tool(self) -> None:
        output, provision_check = self.verify(
            self.sdk("production", provision=True),
            verifier.ADMISSION_PRODUCTION_RELEASE,
        )

        self.assertIn("sdk_profile=production", output)
        provision_check.assert_called_once()
        self.assertNotIn("trust_identity", output)
        self.assertNotIn("release_public_key", output)

    def test_production_profile_requires_provisioning_tool(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "SDK directory is missing"):
            self.verify(
                self.sdk("missing-provision"),
                verifier.ADMISSION_PRODUCTION_RELEASE,
            )

    def test_fixture_requires_marker_and_may_omit_manifest(self) -> None:
        sdk = self.sdk(
            "fixture", provision=True, marked=True, manifest=False
        )

        output, _ = self.verify(sdk, verifier.ADMISSION_TEST_FIXTURE)
        self.assertIn("sdk_release_id=not-applicable-test-fixture", output)

        (
            sdk
            / f"share/cosmo-model-guard/{verifier.TEST_FIXTURE_MARKER_NAME}"
        ).unlink()
        with self.assertRaisesRegex(RuntimeError, "requires the exact"):
            self.verify(sdk, verifier.ADMISSION_TEST_FIXTURE)

    def test_links_and_permissions_are_not_admission_inputs(self) -> None:
        sdk = self.sdk("permissive")
        header = sdk / "include/cosmo_model_guard_v2.h"
        header_target = header.with_name("header-target.h")
        header.rename(header_target)
        os.symlink(header_target.name, header)
        for path in (sdk, sdk / "include", sdk / "lib"):
            path.chmod(0o777)
        header_target.chmod(0o666)
        (sdk / "lib/libcosmo_model_guard.so.2.0.0").chmod(0o777)
        openssl_header = self.openssl_include / "openssl/opensslv.h"
        alias = openssl_header.with_name("alias.h")
        os.link(openssl_header, alias)
        alias.chmod(0o666)

        output, _ = self.verify(sdk, verifier.ADMISSION_PUBLIC_RUNTIME)
        self.assertIn("sdk_profile=public-runtime", output)

    def test_snapshot_detects_content_change_not_mode_change(self) -> None:
        sdk = self.sdk("snapshot")
        output, _ = self.verify(sdk, verifier.ADMISSION_PUBLIC_RUNTIME)
        snapshot = self.verified_root(output)
        staged_header = snapshot / "include/cosmo_model_guard_v2.h"
        staged_header.chmod(0o777)

        repeated, _ = self.verify(
            sdk,
            verifier.ADMISSION_PUBLIC_RUNTIME,
            expected_snapshot=snapshot,
        )
        self.assertEqual(self.verified_root(repeated), snapshot)

        (sdk / "include/cosmo_model_guard_v2.h").write_bytes(b"changed\n")
        with self.assertRaisesRegex(RuntimeError, "expected snapshot root"):
            self.verify(
                sdk,
                verifier.ADMISSION_PUBLIC_RUNTIME,
                expected_snapshot=snapshot,
            )

    def test_snapshot_rejects_unexpected_content(self) -> None:
        sdk = self.sdk("unexpected")
        output, _ = self.verify(sdk, verifier.ADMISSION_PUBLIC_RUNTIME)
        snapshot = self.verified_root(output)
        (snapshot / "unexpected").write_bytes(b"x")

        with self.assertRaisesRegex(RuntimeError, "unexpected or missing"):
            self.verify(
                sdk,
                verifier.ADMISSION_PUBLIC_RUNTIME,
                expected_snapshot=snapshot,
            )

    def test_noncanonical_manifest_is_rejected(self) -> None:
        sdk = self.sdk("extra-key")
        manifest = (
            sdk
            / f"share/cosmo-model-guard/{verifier.SDK_RELEASE_MANIFEST_NAME}"
        )
        manifest.write_bytes(
            manifest.read_bytes() + b"CMG_SDK_BUILD_IMAGE=obsolete\n"
        )

        with self.assertRaisesRegex(RuntimeError, "keys or canonical order"):
            self.verify(sdk, verifier.ADMISSION_PUBLIC_RUNTIME)


if __name__ == "__main__":
    unittest.main()
