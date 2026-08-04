#!/usr/bin/python3
"""Static and inventory tests for SOURCE/production package separation."""

from __future__ import annotations

import hashlib
import importlib.machinery
import importlib.util
import io
import subprocess
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path


REPOSITORY = Path(__file__).resolve().parents[1]
VERIFIER_SOURCE = REPOSITORY / "scripts/verify_package_contents.py"
loader = importlib.machinery.SourceFileLoader(
    "verify_package_contents_under_test", str(VERIFIER_SOURCE)
)
spec = importlib.util.spec_from_loader(loader.name, loader)
if spec is None:
    raise RuntimeError("cannot load package content verifier")
verifier = importlib.util.module_from_spec(spec)
sys.modules[loader.name] = verifier
loader.exec_module(verifier)

EDGE_COMMIT = "1" * 40
ENGINE = b"test-cosmo-engine\n"
VERSION = "V1.2.3"
# Byte-for-byte public synthetic core from Guard's committed CEM v2 golden
# fixture. It contains no production model data or key material.
CEM_V2_GOLDEN_CORE = bytes.fromhex(
    "43454d430002007000010001101112131415161718191a1b1c1d1e1f20212223"
    "2425262728292a2b2c2d2e2f0000000000000007000000006553f10000000087"
    "9d3bf2fd230ad653c8f7570ae04d8536d264ad295da8780467e3c876bb710e59"
    "a0a1a2a3a4a5a6a70000000000000000af0101026e746573745f6d6f64656c"
    "5f303031036656322e302e33045820ea2f32ff5f5bd5b53b578449ac95d1c633"
    "714a6db405e748b4c183d8635d8e030550101112131415161718191a1b1c1d1e"
    "1f0650202122232425262728292a2b2c2d2e2f0707080109010a010b010c1a00"
    "1000000d830200000e8184001400010f818500001400182434d2b420713cb6db"
    "cb299fe6883d793674e92513ddad404ac30c1e3dc988f1ed52383808"
)


def entry(
    kind: str = "file",
    mode: int = 0o644,
    linkname: str | None = None,
    content: bytes | None = None,
    sha256: str | None = None,
    preset_cohort_id: str | None = None,
) -> verifier.ArchiveEntry:
    return verifier.ArchiveEntry(
        kind=kind,
        mode=mode,
        linkname=linkname,
        content=content,
        sha256=sha256,
        preset_cohort_id=preset_cohort_id,
    )


def cem_v2_core_with_cohort(cohort_id: bytes) -> bytes:
    if len(cohort_id) != 16:
        raise ValueError("test cohort ID must be 16 bytes")
    manifest_length = int.from_bytes(CEM_V2_GOLDEN_CORE[60:64], "big")
    manifest = bytearray(
        CEM_V2_GOLDEN_CORE[112 : 112 + manifest_length]
    )
    cohort_tag = b"\x06\x50"
    cohort_offset = manifest.index(cohort_tag) + len(cohort_tag)
    manifest[cohort_offset : cohort_offset + 16] = cohort_id
    core = bytearray(CEM_V2_GOLDEN_CORE)
    core[28:44] = cohort_id
    core[64:96] = hashlib.sha256(manifest).digest()
    core[112 : 112 + manifest_length] = manifest
    return bytes(core)


def build_identity_content() -> bytes:
    engine_sha256 = hashlib.sha256(ENGINE).hexdigest()
    hash_input = (
        f"{verifier.SOURCE_BUILD_IDENTITY_FORMAT}\n"
        f"edge_commit={EDGE_COMMIT}\n"
        f"version={VERSION}\n"
        f"engine_sha256={engine_sha256}\n"
    ).encode("ascii")
    build_identity = hashlib.sha256(hash_input).hexdigest()
    return (
        f"format={verifier.SOURCE_BUILD_IDENTITY_FORMAT}\n"
        f"edge_commit={EDGE_COMMIT}\n"
        f"version={VERSION}\n"
        f"engine_sha256={engine_sha256}\n"
        f"build_identity={build_identity}\n"
    ).encode("ascii")


def common_inventory() -> dict[str, verifier.ArchiveEntry]:
    inventory = {
        path: entry("directory", 0o755)
        for path in verifier.COMMON_DIRECTORIES
    }
    inventory.update({path: entry() for path in verifier.COMMON_FILES})
    inventory.update(
        {
            path: entry("symlink", 0o777, target)
            for path, target in verifier.COMMON_SYMLINKS.items()
        }
    )
    return inventory


def source_inventory() -> dict[str, verifier.ArchiveEntry]:
    inventory = common_inventory()
    inventory.update(
        {path: entry("file", 0o755) for path in verifier.SOURCE_EXECUTABLES}
    )
    inventory.update({path: entry() for path in verifier.SOURCE_FILES})
    inventory[verifier.SOURCE_ENGINE] = entry(
        "file",
        0o755,
        sha256=hashlib.sha256(ENGINE).hexdigest(),
    )
    inventory[verifier.SOURCE_VERSION] = entry(
        content=f"{VERSION}\n".encode("ascii"),
        sha256=hashlib.sha256(f"{VERSION}\n".encode("ascii")).hexdigest(),
    )
    inventory[verifier.SOURCE_BUILD_IDENTITY] = entry(
        content=build_identity_content(),
        sha256=hashlib.sha256(build_identity_content()).hexdigest(),
    )
    return inventory


def production_inventory() -> dict[str, verifier.ArchiveEntry]:
    inventory = common_inventory()
    inventory.update(
        {
            path: entry("file", 0o755)
            for path in verifier.PRODUCTION_EXECUTABLES
        }
    )
    inventory.update({path: entry() for path in verifier.PRODUCTION_FILES})
    return inventory


def write_test_archive(path: Path, members: dict[str, bytes]) -> None:
    package_root = "cosmo-V1.2.3"
    with tarfile.open(path, "w:gz") as package:
        root = tarfile.TarInfo(f"{package_root}/")
        root.type = tarfile.DIRTYPE
        root.mode = 0o755
        package.addfile(root)
        for relative, content in members.items():
            member = tarfile.TarInfo(f"{package_root}/{relative}")
            member.mode = 0o644
            member.size = len(content)
            package.addfile(member, io.BytesIO(content))


class PackageProfileTest(unittest.TestCase):
    def test_source_inventory_requires_public_sdk_and_install_assets(self) -> None:
        inventory = source_inventory()

        self.assertEqual(
            verifier.verify_inventory(inventory, "public-runtime"), "SOURCE"
        )
        self.assertEqual(
            verifier.COMMON_FILES,
            {
                "lib/libcosmo_model_guard.so.2.0.0",
                "share/cosmo-model-guard/cosmo_model_guard_v2.h",
            },
        )
        self.assertEqual(
            verifier.SOURCE_EXECUTABLES,
            {
                "bin/cosmo-engine",
                "install-device.sh",
                "scripts/run_start.sh",
                "scripts/source_health_check.sh",
                "scripts/source_run_start.sh",
                "scripts/stop.sh",
            },
        )
        self.assertEqual(
            verifier.SOURCE_FILES,
            {
                "bin/version.txt",
                "scripts/common.sh",
                "share/cosmo-source/build-identity.env",
                "share/cosmo-source/cosmo.service",
            },
        )

    def test_source_inventory_rejects_missing_or_non_executable_assets(
        self,
    ) -> None:
        for path in sorted(
            verifier.COMMON_FILES
            | verifier.SOURCE_FILES
            | verifier.SOURCE_EXECUTABLES
        ):
            with self.subTest(path=path):
                inventory = source_inventory()
                del inventory[path]
                with self.assertRaisesRegex(
                    verifier.PackageAuditError, "package is missing"
                ):
                    verifier.verify_inventory(inventory, "public-runtime")

        inventory = source_inventory()
        inventory["install-device.sh"] = entry("file", 0o644)
        with self.assertRaisesRegex(
            verifier.PackageAuditError, "has no execute bit"
        ):
            verifier.verify_inventory(inventory, "public-runtime")

    def test_source_inventory_rejects_controlled_release_material(self) -> None:
        forbidden = {
            "bin/cosmo-model-provision",
            "bin/cosmo-release-bootstrap",
            ".release-bootstrap/bin/cosmo-release-bootstrap",
            "scripts/release_updater.sh",
            "scripts/release_updater.py",
            "scripts/release_bootstrap_backend.py",
            "scripts/release_health_check.sh",
            "scripts/install.sh",
            "scripts/inte_run_start.sh",
            "scripts/start.sh",
            "scripts/build_release_secret.py",
            "share/cosmo-factory/cosmo.service",
            "scripts/build_release_bundle.py",
            "trust/release-private-key.pem",
            "resource/nested/commissioning-ed25519.seed",
            "resource/nested/product-model-key-v1.bin",
            "resource/nested/product-pepper-v1.bin",
            "resource/nested/product-pepper-v1.o",
            "resource/nested/cosmo-model-provision",
            "resource/nested/release_bootstrap_backend.py",
            "resource/nested/customer.private.pem",
            "resource/nested/release-private.pem",
            "resource/nested/signing.pk8",
            "resource/nested/signing.jks",
            "share/cosmo-model-guard/device-certificate.bin",
        }
        for path in sorted(forbidden):
            with self.subTest(path=path):
                inventory = source_inventory()
                inventory[path] = entry("file", 0o755)
                with self.assertRaisesRegex(
                    verifier.PackageAuditError,
                    "contains controlled release material",
                ):
                    verifier.verify_inventory(inventory, "public-runtime")

    def test_source_migration_allows_only_legacy_lifecycle_entry_points(self) -> None:
        inventory = source_inventory()
        for path in (
            "scripts/install.sh",
            "scripts/start.sh",
            "scripts/inte_run_start.sh",
        ):
            inventory[path] = entry("file", 0o755)
        self.assertEqual(
            verifier.verify_inventory(inventory, "public-runtime", True), "SOURCE"
        )
        inventory["bin/cosmo-model-provision"] = entry("file", 0o755)
        with self.assertRaises(verifier.PackageAuditError):
            verifier.verify_inventory(inventory, "public-runtime", True)

    def test_source_inventory_requires_one_nonzero_preset_cohort(self) -> None:
        cohort = "11" * 16
        inventory = source_inventory()
        inventory.update(
            {
                "resource/models/current/model.nn": entry(
                    mode=0o644,
                    preset_cohort_id=cohort,
                ),
                "resource/models/future/model.nn": entry(
                    mode=0o644,
                    preset_cohort_id=cohort,
                ),
            }
        )
        self.assertEqual(
            verifier.verify_inventory(inventory, "public-runtime"),
            "SOURCE",
        )

        inventory["resource/models/future/model.nn"] = entry(
            mode=0o644,
            preset_cohort_id="22" * 16,
        )
        with self.assertRaisesRegex(
            verifier.PackageAuditError,
            "mixed cohort IDs",
        ):
            verifier.verify_inventory(inventory, "public-runtime")

        inventory["resource/models/future/model.nn"] = entry(
            mode=0o644,
            preset_cohort_id="0" * 32,
        )
        with self.assertRaisesRegex(
            verifier.PackageAuditError,
            "must be nonzero",
        ):
            verifier.verify_inventory(inventory, "public-runtime")

        inventory["resource/models/future/model.nn"] = entry(mode=0o644)
        with self.assertRaisesRegex(
            verifier.PackageAuditError,
            "lacks strict CEM v2 validation",
        ):
            verifier.verify_inventory(inventory, "public-runtime")

    def test_source_archive_uses_strict_cem_v2_preset_scanner(self) -> None:
        cohort = bytes.fromhex("11" * 16)
        other_cohort = bytes.fromhex("22" * 16)
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / "payload.tar.gz"
            write_test_archive(
                archive,
                {
                    "resource/models/current/model.nn": (
                        cem_v2_core_with_cohort(cohort)
                    ),
                    "resource/models/future/model.nn": (
                        cem_v2_core_with_cohort(cohort)
                    ),
                },
            )
            inventory = verifier.read_inventory(archive)
            verifier.verify_source_preset_cohort(inventory)

            write_test_archive(
                archive,
                {
                    "resource/models/current/model.nn": (
                        cem_v2_core_with_cohort(cohort)
                    ),
                    "resource/models/future/model.nn": (
                        cem_v2_core_with_cohort(other_cohort)
                    ),
                },
            )
            inventory = verifier.read_inventory(archive)
            with self.assertRaisesRegex(
                verifier.PackageAuditError,
                "mixed cohort IDs",
            ):
                verifier.verify_source_preset_cohort(inventory)

            for label, payload in (
                ("zero", cem_v2_core_with_cohort(bytes(16))),
                ("plaintext", b"CENN" + b"\0" * 124),
            ):
                with self.subTest(label=label):
                    write_test_archive(
                        archive,
                        {"resource/models/rejected/model.nn": payload},
                    )
                    with self.assertRaisesRegex(
                        verifier.PackageAuditError,
                        "SOURCE preset model rejected",
                    ):
                        verifier.read_inventory(archive)

    def test_archive_scan_rejects_private_pem_content_with_benign_name(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / "payload.tar.gz"
            write_test_archive(
                archive,
                {
                    "resource/runtime.pem": (
                        b"metadata\n-----BEGIN PRIVATE KEY-----\n"
                        b"not-a-real-key\n-----END PRIVATE KEY-----\n"
                    )
                },
            )
            with self.assertRaisesRegex(
                verifier.PackageAuditError, "PEM private key marker"
            ):
                verifier.read_inventory(archive)

    def test_archive_scan_allows_public_certificate_and_public_key_pem(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / "payload.tar.gz"
            write_test_archive(
                archive,
                {
                    "resource/device-cert.pem": (
                        b"-----BEGIN CERTIFICATE-----\npublic\n"
                        b"-----END CERTIFICATE-----\n"
                    ),
                    "resource/release-public-key.pem": (
                        b"-----BEGIN PUBLIC KEY-----\npublic\n"
                        b"-----END PUBLIC KEY-----\n"
                    ),
                },
            )
            inventory = verifier.read_inventory(archive)
            self.assertEqual(inventory["resource/device-cert.pem"].kind, "file")
            self.assertEqual(
                inventory["resource/release-public-key.pem"].kind,
                "file",
            )
            self.assertFalse(
                verifier._source_path_is_forbidden(
                    "resource/device-cert.pem"
                )
            )
            self.assertFalse(
                verifier._source_path_is_forbidden(
                    "resource/release-public-key.pem"
                )
            )

    def test_source_build_identity_binds_engine_and_version(self) -> None:
        inventory = source_inventory()
        identity = verifier.source_build_identity(inventory)
        self.assertEqual(identity["edge_commit"], EDGE_COMMIT)
        self.assertEqual(
            identity["engine_sha256"], hashlib.sha256(ENGINE).hexdigest()
        )

        inventory[verifier.SOURCE_ENGINE] = entry(
            "file",
            0o755,
            sha256=hashlib.sha256(b"changed-engine\n").hexdigest(),
        )
        with self.assertRaisesRegex(
            verifier.PackageAuditError, "engine SHA-256 differs"
        ):
            verifier.verify_inventory(inventory, "public-runtime")

    def test_source_archive_name_binds_commit_build_and_archive_hash(
        self,
    ) -> None:
        identity = verifier.source_build_identity(source_inventory())
        archive_sha256 = "a" * 64
        valid = Path(
            "/tmp/cosmo-V1.2.3-SOURCE-"
            f"{identity['edge_commit']}-{identity['build_identity']}-"
            f"{archive_sha256}.tar.gz"
        )
        verifier.verify_source_archive_name(valid, identity, archive_sha256)

        with self.assertRaisesRegex(
            verifier.PackageAuditError, "does not bind"
        ):
            verifier.verify_source_archive_name(
                Path("/tmp/cosmo-V1.2.3-SOURCE-wrong.tar.gz"),
                identity,
                archive_sha256,
            )

    def test_production_inventory_contract_is_unchanged_and_source_free(
        self,
    ) -> None:
        inventory = production_inventory()
        self.assertEqual(
            verifier.verify_inventory(inventory, "production-release"),
            "production-release",
        )

        inventory["install-device.sh"] = entry("file", 0o755)
        with self.assertRaisesRegex(
            verifier.PackageAuditError, "SOURCE-only or device-specific"
        ):
            verifier.verify_inventory(inventory, "production-release")

        for path in (
            "share/cosmo-model-guard/device-certificate.bin",
            "resource/nested/commissioning-ed25519.seed",
            "resource/nested/product-model-key-v1.bin",
            "resource/nested/product-pepper-v1.bin",
        ):
            with self.subTest(path=path):
                inventory = production_inventory()
                inventory[path] = entry()
                with self.assertRaisesRegex(
                    verifier.PackageAuditError,
                    "SOURCE-only or device-specific",
                ):
                    verifier.verify_inventory(
                        inventory, "production-release"
                    )

    def test_static_build_wiring_uses_source_label_with_internal_profile(
        self,
    ) -> None:
        rename_script = (
            REPOSITORY / "scripts/package_md5_rename.sh"
        ).read_text(encoding="utf-8")
        build_script = (REPOSITORY / "scripts/build.sh").read_text(
            encoding="utf-8"
        )
        cmake = (REPOSITORY / "CMakeLists.txt").read_text(encoding="utf-8")
        source_unit = (
            REPOSITORY / "config/systemd/cosmo-source.service"
        ).read_text(encoding="utf-8")
        production_unit = (
            REPOSITORY / "config/systemd/cosmo.service"
        ).read_text(encoding="utf-8")

        self.assertIn('public-runtime)\n        label="SOURCE"', rename_script)
        self.assertNotIn('label="UNSIGNED"', rename_script)
        self.assertIn(
            'COSMO_MODEL_GUARD_BUILD_PROFILE="${'
            'COSMO_MODEL_GUARD_BUILD_PROFILE:-public-runtime}"',
            build_script,
        )
        self.assertIn("scripts/verify_package_contents.py", build_script)
        self.assertIn("COSMO_EDGE_SOURCE_COMMIT", build_script)
        self.assertIn(
            'PROJECT_ROOT_PATH="$(cd "$(dirname "$0")/.." && pwd -P)"',
            build_script,
        )
        self.assertIn(
            '${package_name}-${label}${identity_label}-${digest}.tar.gz',
            rename_script,
        )
        self.assertIn(
            'COSMO_MODEL_GUARD_BUILD_PROFILE STREQUAL "public-runtime"',
            cmake,
        )
        self.assertIn("config/systemd/cosmo-source.service", cmake)
        self.assertIn("scripts/source_run_start.sh", cmake)
        self.assertIn("scripts/source_health_check.sh", cmake)
        self.assertIn(
            "Environment=COSMO_SOURCE_RUNTIME=1",
            source_unit,
        )
        self.assertNotIn("COSMO_SOURCE_RUNTIME", production_unit)

    def test_package_labeler_normalizes_archive_metadata(self) -> None:
        package_name = "cosmo-V9.9.9"
        build_epoch = 1_784_776_233

        def write_archive(directory: Path, mtime: int, owner: int) -> Path:
            archive = directory / f"{package_name}.tar.gz"
            with tarfile.open(archive, "w:gz") as package:
                root = tarfile.TarInfo(f"{package_name}/")
                root.type = tarfile.DIRTYPE
                root.mode = 0o755
                root.mtime = mtime
                root.uid = owner
                root.gid = owner
                package.addfile(root)

                content = b"reproducible package\n"
                payload = tarfile.TarInfo(f"{package_name}/payload.txt")
                payload.mode = 0o640
                payload.mtime = mtime
                payload.uid = owner
                payload.gid = owner
                payload.size = len(content)
                package.addfile(payload, io.BytesIO(content))
            return archive

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            first_dir = root / "first"
            second_dir = root / "second"
            first_dir.mkdir()
            second_dir.mkdir()
            write_archive(first_dir, build_epoch + 100, 1000)
            write_archive(second_dir, build_epoch + 200, 2000)

            for directory in (first_dir, second_dir):
                subprocess.run(
                    [
                        str(
                            REPOSITORY
                            / "scripts/package_md5_rename.sh"
                        ),
                        str(directory),
                        package_name,
                        "production-release",
                        str(build_epoch),
                    ],
                    check=True,
                    capture_output=True,
                    text=True,
                )

            first = next(first_dir.glob("*-FACTORY-BASE-*.tar.gz"))
            second = next(second_dir.glob("*-FACTORY-BASE-*.tar.gz"))
            self.assertEqual(first.name, second.name)
            self.assertEqual(first.read_bytes(), second.read_bytes())

            with tarfile.open(first, "r:gz") as package:
                for member in package.getmembers():
                    self.assertEqual(member.mtime, build_epoch)
                    self.assertEqual(member.uid, 0)
                    self.assertEqual(member.gid, 0)


if __name__ == "__main__":
    unittest.main()
