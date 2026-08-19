import hashlib
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "media_sysroot_lock", ROOT / "tools" / "rknn" / "media_sysroot_lock.py"
)
assert SPEC and SPEC.loader
media_lock = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(media_lock)


def digest(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


class RknnMediaSysrootLockTest(unittest.TestCase):
    def make_fixture(
        self, directory: str, *, require_seal: bool = True
    ) -> tuple[Path, Path]:
        root = Path(directory)
        sysroot = root / "sysroot"
        (sysroot / "include" / "rockchip").mkdir(parents=True)
        (sysroot / "include" / "rga").mkdir(parents=True)
        (sysroot / "lib").mkdir(parents=True)
        values = {
            "include/rockchip/rk_mpi.h": b"mpp-header",
            "include/rga/im2d.h": b"rga-header",
            "lib/librockchip_mpp.so.0": b"mpp-library",
            "lib/librga.so": b"rga-library",
        }
        for relative, value in values.items():
            (sysroot / relative).write_bytes(value)
        (sysroot / "lib" / "librockchip_mpp.so.1").symlink_to(
            "librockchip_mpp.so.0"
        )
        (sysroot / "lib" / "librockchip_mpp.so").symlink_to(
            "librockchip_mpp.so.1"
        )

        config = root / "config"
        profile_dir = config / "rknn" / "platforms"
        lock_dir = config / "rockchip-media"
        profile_dir.mkdir(parents=True)
        lock_dir.mkdir(parents=True)
        profile = profile_dir / "fixture.json"
        profile.write_text(
            json.dumps(
                {
                    "chip": "fixture",
                    "media": {
                        "default_backend": "rockchip",
                        "runtime_lock": "../../rockchip-media/runtime-lock.json",
                        "runtime_profile": "shared",
                        "require_sealed_sysroot": require_seal,
                    },
                }
            ),
            encoding="utf-8",
        )
        (lock_dir / "runtime-lock.json").write_text(
            json.dumps(
                {
                    "runtimes": {
                        "shared": {
                            "sources": {
                                "mpp": {"revision": "mpp-revision"},
                                "rga": {"revision": "rga-revision"},
                            },
                            "artifacts": {
                                relative: {
                                    "sha256": digest(value),
                                    **(
                                        {
                                            "elf": {
                                                "machine": "AArch64",
                                                "soname": (
                                                    "librockchip_mpp.so.1"
                                                    if "mpp.so" in relative
                                                    else "librga.so"
                                                ),
                                            }
                                        }
                                        if relative.startswith("lib/")
                                        else {}
                                    ),
                                }
                                for relative, value in values.items()
                            },
                            "links": {
                                "lib/librockchip_mpp.so": "librockchip_mpp.so.1",
                                "lib/librockchip_mpp.so.1": "librockchip_mpp.so.0",
                            },
                        }
                    }
                }
            ),
            encoding="utf-8",
        )
        return profile, sysroot

    @mock.patch.object(
        media_lock,
        "inspect_elf",
        side_effect=lambda path: {
            "machine": "AArch64",
            "soname": (
                "librockchip_mpp.so.1"
                if "rockchip_mpp" in path.name
                else "librga.so"
            ),
        },
    )
    def test_seal_and_verify_shared_runtime(self, _inspect: mock.Mock) -> None:
        with tempfile.TemporaryDirectory() as directory:
            profile, sysroot = self.make_fixture(directory)
            sources = {"mpp": "mpp-revision", "rga": "rga-revision"}
            manifest = media_lock.seal_sysroot(profile, sysroot, sources)
            self.assertEqual(manifest.name, media_lock.MANIFEST_NAME)
            result = media_lock.verify_sysroot(profile, sysroot)
            self.assertEqual(result["runtime_profile"], "shared")
            self.assertEqual(result["manifest"], manifest)

            (sysroot / "lib" / "librga.so").write_bytes(b"changed")
            with self.assertRaisesRegex(
                media_lock.MediaSysrootError, "hash mismatch"
            ):
                media_lock.verify_sysroot(profile, sysroot)

    @mock.patch.object(
        media_lock,
        "inspect_elf",
        return_value={"machine": "AArch64", "soname": "librockchip_mpp.so.1"},
    )
    def test_seal_rejects_source_mismatch(self, _inspect: mock.Mock) -> None:
        with tempfile.TemporaryDirectory() as directory:
            profile, sysroot = self.make_fixture(directory)
            with self.assertRaisesRegex(
                media_lock.MediaSysrootError, "source revisions"
            ):
                media_lock.seal_sysroot(
                    profile,
                    sysroot,
                    {"mpp": "wrong", "rga": "rga-revision"},
                )

    @mock.patch.object(
        media_lock,
        "inspect_elf",
        side_effect=lambda path: {
            "machine": "AArch64",
            "soname": (
                "librockchip_mpp.so.1"
                if "rockchip_mpp" in path.name
                else "librga.so"
            ),
        },
    )
    def test_unsealed_legacy_runtime_remains_admissible(
        self, _inspect: mock.Mock
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            profile, sysroot = self.make_fixture(directory, require_seal=False)
            result = media_lock.verify_sysroot(profile, sysroot)
            self.assertIsNone(result["manifest"])

    def test_source_date_epoch_makes_manifest_timestamp_reproducible(self) -> None:
        with mock.patch.dict("os.environ", {"SOURCE_DATE_EPOCH": "0"}):
            self.assertEqual(
                media_lock.manifest_created_at(), "1970-01-01T00:00:00+00:00"
            )

        with mock.patch.dict("os.environ", {"SOURCE_DATE_EPOCH": "invalid"}):
            with self.assertRaisesRegex(
                media_lock.MediaSysrootError, "must be an integer"
            ):
                media_lock.manifest_created_at()

        with mock.patch.dict("os.environ", {"SOURCE_DATE_EPOCH": "-1"}):
            with self.assertRaisesRegex(
                media_lock.MediaSysrootError, "must not be negative"
            ):
                media_lock.manifest_created_at()


if __name__ == "__main__":
    unittest.main()
