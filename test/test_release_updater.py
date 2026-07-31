#!/usr/bin/python3
"""Offline fault/security tests for the signed release transaction."""

from __future__ import annotations

import contextlib
import hashlib
import importlib.machinery
import importlib.util
import io
import json
import os
import re
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
import types
import unittest
from unittest import mock
from pathlib import Path
from typing import Any, Mapping, Sequence


sys.dont_write_bytecode = True
REPOSITORY = Path(__file__).resolve().parents[1]
UPDATER_SOURCE = REPOSITORY / "scripts/release_updater.py"
BACKEND_SOURCE = REPOSITORY / "scripts/release_bootstrap_backend.py"
PACKAGER = REPOSITORY / "scripts/build_release_bundle.py"
PUBLIC_KEY_OBJECT_GENERATOR = REPOSITORY / "scripts/build_release_public_key_object.py"
loader = importlib.machinery.SourceFileLoader("release_updater_under_test", str(UPDATER_SOURCE))
spec = importlib.util.spec_from_loader(loader.name, loader)
if spec is None:
    raise RuntimeError("cannot load release updater")
release = importlib.util.module_from_spec(spec)
sys.modules[loader.name] = release
loader.exec_module(release)
backend_loader = importlib.machinery.SourceFileLoader(
    "release_bootstrap_backend_under_test", str(BACKEND_SOURCE)
)
backend_spec = importlib.util.spec_from_loader(backend_loader.name, backend_loader)
if backend_spec is None:
    raise RuntimeError("cannot load release bootstrap backend")
backend = importlib.util.module_from_spec(backend_spec)
sys.modules[backend_loader.name] = backend
backend_loader.exec_module(backend)
packager_loader = importlib.machinery.SourceFileLoader(
    "release_packager_under_test", str(PACKAGER)
)
packager_spec = importlib.util.spec_from_loader(
    packager_loader.name, packager_loader
)
if packager_spec is None:
    raise RuntimeError("cannot load release packager")
packager = importlib.util.module_from_spec(packager_spec)
sys.modules[packager_loader.name] = packager
packager_loader.exec_module(packager)


def run(arguments: Sequence[str], cwd: Path | None = None) -> subprocess.CompletedProcess[bytes]:
    return subprocess.run(
        list(arguments),
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        env={"LC_ALL": "C", "PATH": "/usr/bin:/bin"},
    )


def write(path: Path, data: bytes, mode: int = 0o644) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    os.chmod(path, mode)


MODEL_GUARD_TRUST_SECTIONS = {
    "cmg_product_pepper_bundle_v1": ".cmg.trust.product.v1",
    "cmg_commissioning_public_key_bundle_v1": ".cmg.trust.commissioning.v1",
}
MODEL_GUARD_TRUST_LOGICAL_NAMES = {
    "cmg_product_pepper_bundle_v1": "product_pepper_bundle",
    "cmg_commissioning_public_key_bundle_v1": "commissioning_public_key_bundle",
}

LIBCRYPTO = REPOSITORY / "build/thirdparty_install/openssl/lib/libcrypto.so.3"
LIBSSL = REPOSITORY / "build/thirdparty_install/openssl/lib/libssl.so.3"
SYNTHETIC_SOPHON_LIBRARIES = {
    "libbmlib.so": b"synthetic-bmlib-link-runtime",
    "libbmlib.so.0": b"synthetic-bmlib-versioned-runtime",
    "libbmrt.so": b"synthetic-bmrt-link-runtime",
    "libbmrt.so.1.0": b"synthetic-bmrt-versioned-runtime",
}
# Byte-for-byte ``core.core_hex`` from Model Guard's committed
# testdata/cem_v2_golden.json.  This is public synthetic test-only material,
# not a production model or key.
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
CEM_V2_GOLDEN_SHA256 = (
    "fd03787c4de05ba3b8a99c7afcd27437dc60dc8aa9e4c86be8fb0f6b71f07380"
)


def ed25519_raw_public_key(public_key: Path) -> bytes:
    result = run(
        (
            "/usr/bin/openssl",
            "pkey",
            "-pubin",
            "-in",
            str(public_key),
            "-outform",
            "DER",
        )
    )
    if result.returncode != 0 or len(result.stdout) != 44:
        raise RuntimeError(result.stderr.decode())
    self_describing_prefix = bytes.fromhex("302a300506032b6570032100")
    if not result.stdout.startswith(self_describing_prefix):
        raise RuntimeError("test key is not Ed25519")
    return result.stdout[len(self_describing_prefix) :]


def cem_v2_golden_manifest() -> bytes:
    manifest_len = int.from_bytes(CEM_V2_GOLDEN_CORE[60:64], "big")
    return CEM_V2_GOLDEN_CORE[112 : 112 + manifest_len]


def cem_v2_core_with_manifest(manifest: bytes) -> bytes:
    original_manifest_len = int.from_bytes(CEM_V2_GOLDEN_CORE[60:64], "big")
    payload = CEM_V2_GOLDEN_CORE[112 + original_manifest_len :]
    preamble = bytearray(CEM_V2_GOLDEN_CORE[:112])
    preamble[60:64] = len(manifest).to_bytes(4, "big")
    preamble[64:96] = hashlib.sha256(manifest).digest()
    return bytes(preamble) + manifest + payload


def cem_v2_core_with_cohort(cohort_id: bytes) -> bytes:
    if len(cohort_id) != 16:
        raise ValueError("test cohort ID must be 16 bytes")
    manifest = bytearray(cem_v2_golden_manifest())
    cohort_tag = b"\x06\x50"
    cohort_offset = manifest.index(cohort_tag) + len(cohort_tag)
    manifest[cohort_offset : cohort_offset + 16] = cohort_id
    core = bytearray(cem_v2_core_with_manifest(bytes(manifest)))
    core[28:44] = cohort_id
    return bytes(core)


def valid_model_guard_header() -> bytes:
    return (
        b"#define CMG_V2_ABI_MAJOR UINT32_C(2)\n"
        b"#define CMG_V2_ARTIFACT_INFO_SIZE UINT32_C(72)\n"
        b"#define CMG_V2_SOPHON_LOAD_OPTIONS_SIZE UINT32_C(16)\n"
        b"int CmgV2OpenArtifact(void);\n"
        b"int CmgV2GetArtifactInfo(void);\n"
        b"int CmgV2LoadSophonSegment(void);\n"
        b"void CmgV2CloseArtifact(void);\n"
    )


def c_object_array(
    name: str,
    value: bytes,
    *,
    hidden: bool = True,
    section: str | None = None,
) -> str:
    initializer = ",".join(f"0x{item:02x}" for item in value)
    attributes = ['visibility("hidden")'] if hidden else []
    attributes.append("used")
    if section is not None:
        attributes.extend((f'section("{section}")', "aligned(16)"))
    return (
        f"__attribute__(({','.join(attributes)})) "
        f"const unsigned char {name}[{len(value)}] = {{{initializer}}};\n"
    )


def valid_model_guard_trust_objects() -> dict[str, bytes]:
    pepper = bytes(range(1, 33))
    pepper_id = hashlib.sha256(
        b"cosmo-product-pepper-key-id-v1" + pepper
    ).digest()[:16]
    product_bundle = (
        b"CMPB\x00\x01\x00\x40"
        + b"\x00" * 8
        + pepper_id
        + pepper
    )
    commissioning_key = bytes(range(33, 65))
    commissioning_id = hashlib.sha256(
        b"cosmo-commissioning-key-id-v1\x00\x01" + commissioning_key
    ).digest()[:16]
    commissioning_bundle = (
        b"CMKB\x00\x01\x00\x40"
        + b"\x00" * 8
        + commissioning_id
        + commissioning_key
    )
    return {
        "cmg_product_pepper_bundle_v1": product_bundle,
        "cmg_commissioning_public_key_bundle_v1": commissioning_bundle,
    }


class Fixture:
    def __init__(self, root: Path) -> None:
        self.root = root
        self.install = root / "install"
        self.persistent = root / "persistent/model-guard"
        self.private_key = root / "ephemeral-release-private.pem"
        self.public_key = root / "ephemeral-release-public.pem"
        self.paths = release.ReleasePaths(
            install_root=self.install,
            model_guard_state_root=self.persistent,
            openssl=Path("/usr/bin/openssl"),
        )
        write(
            self.paths.stable_health_script,
            (REPOSITORY / "scripts/release_health_check.sh").read_bytes(),
            0o755,
        )
        result = run(
            (
                "/usr/bin/openssl",
                "genpkey",
                "-algorithm",
                "ED25519",
                "-out",
                str(self.private_key),
            )
        )
        if result.returncode != 0:
            raise RuntimeError(result.stderr.decode())
        result = run(
            (
                "/usr/bin/openssl",
                "pkey",
                "-in",
                str(self.private_key),
                "-pubout",
                "-out",
                str(self.public_key),
            )
        )
        if result.returncode != 0:
            raise RuntimeError(result.stderr.decode())
        os.chmod(self.private_key, 0o600)
        os.chmod(self.public_key, 0o600)
        write(
            self.persistent / "device-certificate.bin",
            b"CMDC-test-device-certificate",
            0o600,
        )
        os.chmod(self.persistent, 0o700)

    def payload(
        self,
        name: str,
        *,
        plaintext_preset: bool = False,
        wrong_bootstrap_trust: bool = False,
        omit_model_provision: bool = False,
        model_provision_mode: int = 0o755,
        model_guard_test_marker: bool = False,
        zero_model_guard_trust: bool = False,
        omit_model_guard_trust_symbol: str | None = None,
        bootstrap_link_crypto: bool = True,
        bootstrap_runpath: str | None = "$ORIGIN/../lib",
        bootstrap_old_dtags: bool = False,
        bootstrap_extra_needed: bool = False,
        health_script_body: bytes | None = None,
    ) -> Path:
        payload = self.root / f"payload-{name}"
        payload.mkdir()
        guard_trust = valid_model_guard_trust_objects()
        if zero_model_guard_trust:
            guard_trust = {
                symbol: b"\x00" * len(value) for symbol, value in guard_trust.items()
            }
        if omit_model_guard_trust_symbol is not None:
            guard_trust.pop(omit_model_guard_trust_symbol)
        guard_source = self.root / f"guard-{name}.c"
        guard_source.write_text(
            "int CmgV2OpenArtifact(void){return 0;}\n"
            "int CmgV2GetArtifactInfo(void){return 0;}\n"
            "int CmgV2LoadSophonSegment(void){return 0;}\n"
            "int CmgV2CloseArtifact(void){return 0;}\n"
            + "".join(
                c_object_array(
                    symbol,
                    value,
                    section=MODEL_GUARD_TRUST_SECTIONS[symbol],
                )
                for symbol, value in guard_trust.items()
            ),
            encoding="ascii",
        )
        version_script = self.root / f"guard-{name}.map"
        version_script.write_text(
            "COSMO_GUARD_2 { global: CmgV2OpenArtifact; CmgV2GetArtifactInfo; "
            "CmgV2LoadSophonSegment; CmgV2CloseArtifact; local: *; };\n",
            encoding="ascii",
        )
        trust_linker_script = self.root / f"guard-{name}-trust.ld"
        trust_linker_script.write_text(
            "SECTIONS {\n"
            " .cmg.trust.product.v1 : ALIGN(16) { KEEP(*(.cmg.trust.product.v1)) }\n"
            " .cmg.trust.commissioning.v1 : ALIGN(16) { KEEP(*(.cmg.trust.commissioning.v1)) }\n"
            "}\nINSERT AFTER .rodata;\n",
            encoding="ascii",
        )
        guard = payload / f"lib/{release.GUARD_REAL_FILENAME}"
        guard.parent.mkdir(parents=True)
        result = run(
            (
                "/usr/bin/aarch64-linux-gnu-gcc",
                "-fPIC",
                "-shared",
                str(guard_source),
                f"-Wl,--version-script={version_script}",
                f"-Wl,-T,{trust_linker_script}",
                "-Wl,-z,separate-code",
                "-Wl,-soname,libcosmo_model_guard.so.2",
                "-o",
                str(guard),
            )
        )
        if result.returncode != 0:
            raise RuntimeError(result.stderr.decode())
        result = run(
            ("/usr/bin/aarch64-linux-gnu-strip", "--strip-all", str(guard))
        )
        if result.returncode != 0:
            raise RuntimeError(result.stderr.decode())
        os.chmod(guard, 0o755)
        os.symlink(release.GUARD_REAL_FILENAME, payload / f"lib/{release.GUARD_SONAME}")
        os.symlink(release.GUARD_SONAME, payload / "lib/libcosmo_model_guard.so")

        engine_source = self.root / f"engine-{name}.c"
        engine_source.write_text(
            "extern int CmgV2OpenArtifact(void); int main(void){return CmgV2OpenArtifact();}\n",
            encoding="ascii",
        )
        engine = payload / "bin/cosmo-engine"
        engine.parent.mkdir(parents=True)
        result = run(
            (
                "/usr/bin/aarch64-linux-gnu-gcc",
                str(engine_source),
                f"-L{payload / 'lib'}",
                "-lcosmo_model_guard",
                "-Wl,-rpath,$ORIGIN/../lib",
                "-o",
                str(engine),
            )
        )
        if result.returncode != 0:
            raise RuntimeError(result.stderr.decode())
        os.chmod(engine, 0o755)
        public_der = run(
            (
                "/usr/bin/openssl",
                "pkey",
                "-pubin",
                "-in",
                str(self.public_key),
                "-outform",
                "DER",
            )
        )
        if public_der.returncode != 0:
            raise RuntimeError(public_der.stderr.decode())
        trusted_raw_key = public_der.stdout[-32:]
        raw_key = bytearray(trusted_raw_key)
        if wrong_bootstrap_trust:
            raw_key[0] ^= 1
        key_id = hashlib.sha256(
            b"cosmo-release-key-id-v1" + (1).to_bytes(2, "big") + trusted_raw_key
        ).digest()[:16]
        pem_sha256 = hashlib.sha256(self.public_key.read_bytes()).digest()

        bootstrap_source = self.root / f"release-bootstrap-{name}.c"
        bootstrap_main = "int main(void){return 1;}\n"
        if bootstrap_link_crypto:
            bootstrap_main = (
                "extern unsigned long OpenSSL_version_num(void);\n"
                "int main(void){return OpenSSL_version_num()==0UL;}\n"
            )
        if bootstrap_extra_needed:
            bootstrap_main = (
                "extern unsigned long OpenSSL_version_num(void);\n"
                "extern double cos(double); volatile double input_value=0.5;\n"
                "int main(void){return OpenSSL_version_num()==0UL || "
                "cos(input_value)==2.0;}\n"
            )
        bootstrap_source.write_text(
            c_object_array("cosmo_release_public_key_raw_v1", bytes(raw_key))
            + c_object_array("cosmo_release_public_key_id_v1", key_id)
            + c_object_array("cosmo_release_public_key_pem_sha256_v1", pem_sha256)
            + bootstrap_main,
            encoding="ascii",
        )
        bootstrap = payload / release.RELEASE_BOOTSTRAP_PATH
        bootstrap.parent.mkdir(parents=True, exist_ok=True)
        bootstrap_arguments = [
            "/usr/bin/aarch64-linux-gnu-gcc",
            str(bootstrap_source),
            "-Wl,-z,relro",
            "-Wl,-z,now",
            "-Wl,-z,noexecstack",
        ]
        if bootstrap_old_dtags:
            bootstrap_arguments.append("-Wl,--disable-new-dtags")
        else:
            bootstrap_arguments.append("-Wl,--enable-new-dtags")
        if bootstrap_runpath is not None:
            bootstrap_arguments.append(f"-Wl,-rpath,{bootstrap_runpath}")
        if bootstrap_link_crypto:
            bootstrap_arguments.extend(
                (
                    f"-L{REPOSITORY / 'build/thirdparty_install/openssl/lib'}",
                    "-Wl,--no-as-needed",
                    "-lcrypto",
                    "-Wl,--as-needed",
                )
            )
        if bootstrap_extra_needed:
            bootstrap_arguments.extend(("-fno-builtin-cos", "-lm"))
        bootstrap_arguments.extend(("-o", str(bootstrap)))
        result = run(bootstrap_arguments)
        if result.returncode != 0:
            raise RuntimeError(result.stderr.decode())
        os.chmod(bootstrap, 0o755)
        if not omit_model_provision:
            provision_source = self.root / f"model-provision-{name}.c"
            provision_source.write_text(
                "int main(void){return 1;}\n",
                encoding="ascii",
            )
            provision_path = payload / release.MODEL_PROVISION_PATH
            provision_path.parent.mkdir(parents=True, exist_ok=True)
            result = run(
                (
                    "/usr/bin/aarch64-linux-gnu-gcc",
                    str(provision_source),
                    "-o",
                    str(provision_path),
                )
            )
            if result.returncode != 0:
                raise RuntimeError(result.stderr.decode())
            os.chmod(provision_path, model_provision_mode)
        for filename, data in SYNTHETIC_SOPHON_LIBRARIES.items():
            write(payload / "lib" / filename, data, 0o755)
        write(payload / "lib/libcrypto.so.3", LIBCRYPTO.read_bytes(), 0o755)
        write(payload / "lib/libssl.so.3", LIBSSL.read_bytes(), 0o755)
        header = valid_model_guard_header()
        write(payload / release.GUARD_HEADER_PATH, header)
        if model_guard_test_marker:
            write(
                payload
                / "share/cosmo-model-guard/TEST_FIXTURE_DO_NOT_DEPLOY",
                b"COSMO_MODEL_GUARD_V2_TEST_FIXTURE_DO_NOT_DEPLOY\n",
            )
        model = (
            b"CENN" + b"plaintext"
            if plaintext_preset
            else CEM_V2_GOLDEN_CORE
        )
        model_path = payload / "resource/models/preset-one/model.nn"
        write(model_path, model)
        for script in (
            "common.sh",
            "install.sh",
            "inte_run_start.sh",
            "release_bootstrap_backend.py",
            "release_health_check.sh",
            "release_updater.py",
            "release_updater.sh",
            "run_start.sh",
            "start.sh",
            "stop.sh",
        ):
            source = REPOSITORY / "scripts" / script
            destination = payload / "scripts" / script
            destination.parent.mkdir(parents=True, exist_ok=True)
            if script == "release_health_check.sh" and health_script_body is not None:
                write(destination, health_script_body, 0o755)
            else:
                shutil.copy2(source, destination)
                os.chmod(destination, 0o755 if script.endswith(".sh") else 0o644)
        for directory in ("files", "font", "web"):
            (payload / directory).mkdir()
        return payload

    def package(
        self,
        name: str,
        generation: int,
        *,
        plaintext_preset: bool = False,
        wrong_bootstrap_trust: bool = False,
        omit_model_provision: bool = False,
        model_provision_mode: int = 0o755,
        model_guard_test_marker: bool = False,
        zero_model_guard_trust: bool = False,
        omit_model_guard_trust_symbol: str | None = None,
        bootstrap_link_crypto: bool = True,
        bootstrap_runpath: str | None = "$ORIGIN/../lib",
        bootstrap_old_dtags: bool = False,
        bootstrap_extra_needed: bool = False,
        health_script_body: bytes | None = None,
    ) -> Path:
        payload = self.payload(
            name,
            plaintext_preset=plaintext_preset,
            wrong_bootstrap_trust=wrong_bootstrap_trust,
            omit_model_provision=omit_model_provision,
            model_provision_mode=model_provision_mode,
            model_guard_test_marker=model_guard_test_marker,
            zero_model_guard_trust=zero_model_guard_trust,
            omit_model_guard_trust_symbol=omit_model_guard_trust_symbol,
            bootstrap_link_crypto=bootstrap_link_crypto,
            bootstrap_runpath=bootstrap_runpath,
            bootstrap_old_dtags=bootstrap_old_dtags,
            bootstrap_extra_needed=bootstrap_extra_needed,
            health_script_body=health_script_body,
        )
        return self.package_existing(payload, name, generation)

    def package_existing(
        self,
        payload: Path,
        name: str,
        generation: int,
        *,
        signing_key_bytes: bytes | None = None,
        extra_environment: Mapping[str, str] | None = None,
    ) -> Path:
        archive = self.root / f"cosmo-release-{name}.tar.gz"
        read_fd, write_fd = os.pipe()
        private_bytes = (
            self.private_key.read_bytes()
            if signing_key_bytes is None
            else signing_key_bytes
        )
        os.write(write_fd, private_bytes)
        os.close(write_fd)
        saved_three: int | None
        try:
            saved_three = os.dup(3)
        except OSError:
            saved_three = None
        try:
            os.dup2(read_fd, 3)
            environment = {"LC_ALL": "C", "PATH": "/usr/bin:/bin"}
            if extra_environment is not None:
                environment.update(extra_environment)
            result = subprocess.run(
                (
                    "/usr/bin/python3",
                    "-I",
                    "-B",
                    str(PACKAGER),
                    "--payload",
                    str(payload),
                    "--output",
                    str(archive),
                    "--release-id",
                    name,
                    "--generation",
                    str(generation),
                    "--release-public-key",
                    str(self.public_key),
                ),
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
                pass_fds=(3,),
                env=environment,
            )
        finally:
            os.close(read_fd)
            if saved_three is None:
                with contextlib.suppress(OSError):
                    os.close(3)
            else:
                os.dup2(saved_three, 3)
                os.close(saved_three)
            private_copy = bytearray(private_bytes)
            for index in range(len(private_copy)):
                private_copy[index] = 0
        if result.returncode != 0:
            raise RuntimeError(result.stderr.decode())
        os.chmod(archive, 0o600)
        return archive

    def archive_release_tree(self, archive: Path) -> tuple[Path, dict[str, Any]]:
        destination = self.root / f"unpacked-{archive.stem.replace('.', '-')}"
        destination.mkdir()
        with tarfile.open(archive, "r:gz") as bundle:
            bundle.extractall(destination)
        payload = destination / "payload"
        os.rename(destination / "meta", payload / "meta")
        manifest = json.loads((payload / "meta/compatibility.manifest.json").read_text(encoding="utf-8"))
        return payload, manifest

    def sign_release_manifest(self, manifest: Mapping[str, Any]) -> tuple[bytes, bytes]:
        manifest_bytes = release._canonical_json(manifest)
        message = self.root / f"release-manifest-{os.urandom(8).hex()}"
        signature = message.with_suffix(".sig")
        write(message, manifest_bytes, 0o600)
        result = run(
            (
                "/usr/bin/openssl",
                "pkeyutl",
                "-sign",
                "-rawin",
                "-inkey",
                str(self.private_key),
                "-in",
                str(message),
                "-out",
                str(signature),
            )
        )
        if result.returncode != 0:
            raise RuntimeError(result.stderr.decode())
        return manifest_bytes, signature.read_bytes()

    def bootstrap_verified_for_test(
        self,
        updater: release.ReleaseUpdater,
        payload: Path,
        manifest: Mapping[str, Any],
    ) -> None:
        with updater._lock():
            release_id = release._validate_release_id(manifest["release_id"])
            destination = updater.paths.releases / release_id
            if destination.exists():
                release._fail("bootstrap release already exists")
            shutil.copytree(payload, destination, symlinks=True)
            meta = destination / "meta"
            meta.mkdir(mode=0o700, exist_ok=True)
            key_path = meta / "release-public-key.pem"
            key_path.write_bytes(self.public_key.read_bytes())
            os.chmod(key_path, 0o600)
            manifest_bytes = (meta / "compatibility.manifest.json").read_bytes()
            state = {
                "current_release_id": release_id,
                "format": release.STATE_FORMAT,
                "manifest_sha256": release._sha256_bytes(manifest_bytes),
                "release_generation": manifest["release_generation"],
                "release_key_id": manifest["release_key"]["id"],
                "release_public_key_sha256": manifest["release_key"][
                    "public_key_sha256"
                ],
            }
            updater._switch_current(release_id)
            for name in release.FACADE_DIRECTORIES:
                facade = updater.paths.install_root / name
                if facade.exists() or facade.is_symlink():
                    release._fail("bootstrap facade already exists")
                os.symlink(f"current/{name}", facade)
            release._fsync_directory(updater.paths.install_root)
            release._atomic_write(
                updater.paths.state_file,
                release._canonical_json(state),
                0o600,
            )

    def bootstrap(self, archive: Path) -> release.ReleaseUpdater:
        payload, manifest = self.archive_release_tree(archive)
        updater = release.ReleaseUpdater(self.paths)
        self.bootstrap_verified_for_test(updater, payload, manifest)
        return updater


def rewrite_archive(source: Path, destination: Path, mutation) -> None:
    with tarfile.open(source, "r:gz") as original:
        members: list[tuple[tarfile.TarInfo, bytes | None]] = []
        for member in original:
            data = original.extractfile(member).read() if member.isfile() else None
            members.append((member, data))
    mutation(members)
    with tarfile.open(destination, "w:gz", format=tarfile.PAX_FORMAT) as changed:
        for member, data in members:
            changed.addfile(member, io.BytesIO(data) if data is not None else None)
    os.chmod(destination, 0o600)


class ReleaseUpdaterTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="cosmo-release-tests-")
        self.fixture = Fixture(Path(self.temporary.name))

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_validation_tool_allows_a_system_symlink(self) -> None:
        target = Path(self.temporary.name) / "tool"
        target.symlink_to("/usr/bin/true")
        release._validate_executable_tool(target)

    def test_streaming_digest_rejects_path_replacement(self) -> None:
        path = Path(self.temporary.name) / "runtime.so"
        replacement = Path(self.temporary.name) / "runtime.so.replacement"
        write(path, b"A" * 4096, 0o644)
        write(replacement, b"B" * 4096, 0o644)
        original_read = os.read
        replaced = False

        def read_and_replace(fd: int, size: int) -> bytes:
            nonlocal replaced
            block = original_read(fd, size)
            if not replaced:
                os.replace(replacement, path)
                replaced = True
            return block

        with mock.patch.object(
            release.os, "read", side_effect=read_and_replace
        ):
            with self.assertRaisesRegex(
                release.ReleaseError, "file changed or exceeded limit"
            ):
                release._sha256_limited(path, 8192)
        self.assertTrue(replaced)

    def _embedded_bootstrap_inputs(
        self, archive: Path, fixture: Fixture | None = None
    ) -> tuple[bytes, bytes, bytes, bytes, bytes]:
        selected_fixture = self.fixture if fixture is None else fixture
        with tarfile.open(archive, "r:gz") as bundle:
            manifest_stream = bundle.extractfile("meta/compatibility.manifest.json")
            signature_stream = bundle.extractfile("meta/compatibility.manifest.sig")
            assert manifest_stream is not None and signature_stream is not None
            manifest = manifest_stream.read()
            signature = signature_stream.read()
        public_key = selected_fixture.public_key.read_bytes()
        der = run(
            (
                "/usr/bin/openssl",
                "pkey",
                "-pubin",
                "-in",
                str(selected_fixture.public_key),
                "-outform",
                "DER",
            )
        )
        self.assertEqual(der.returncode, 0, der.stderr.decode())
        raw_key = der.stdout[-32:]
        key_id = hashlib.sha256(
            b"cosmo-release-key-id-v1" + (1).to_bytes(2, "big") + raw_key
        ).digest()[:16]
        return manifest, signature, public_key, raw_key, key_id

    def _create_legacy_layout(self, fixture: Fixture | None = None) -> None:
        selected_fixture = self.fixture if fixture is None else fixture
        selected_fixture.install.mkdir(mode=0o755, exist_ok=True)
        for name in release.FACADE_DIRECTORIES:
            (selected_fixture.install / name).mkdir(mode=0o755)
        write(selected_fixture.install / "bin/cosmo-engine", b"legacy-engine", 0o755)
        write(
            selected_fixture.install / "scripts/inte_run_start.sh",
            b"#!/bin/sh\n# legacy-start-marker\n",
            0o755,
        )

    def test_embedded_verifier_bootstrap_uses_journaled_per_facade_exchanges(self) -> None:
        archive = self.fixture.package("release-one", 1)
        archive_hardlink = self.fixture.root / "bootstrap-operator-copy.tar.gz"
        os.link(archive, archive_hardlink)
        os.chmod(archive, 0o666)
        if os.geteuid() == 0:
            os.chown(archive, 65534, 65534)
        self._create_legacy_layout()
        manifest, signature, public_key, raw_key, key_id = self._embedded_bootstrap_inputs(
            archive
        )
        updater = release.ReleaseUpdater(self.fixture.paths)
        updater._run_bootstrap_health_gate = lambda *_: None
        stop_calls: list[Path] = []

        def stop_before_migration(candidate: Path) -> None:
            self.assertEqual(candidate, self.fixture.paths.releases / "release-one")
            self.assertFalse(self.fixture.paths.legacy_backup.exists())
            for name in release.FACADE_DIRECTORIES:
                self.assertTrue((self.fixture.install / name).is_dir())
                self.assertFalse((self.fixture.install / name).is_symlink())
            stop_calls.append(candidate)

        updater._run_signed_candidate_stop = stop_before_migration
        before = release._state_tree_fingerprint(self.fixture.persistent)
        archive_fd = os.open(archive, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW)
        try:
            installed = updater.bootstrap_from_embedded_verifier(
                archive_fd,
                manifest,
                signature,
                public_key,
                raw_key,
                key_id,
                hashlib.sha256(public_key).digest(),
            )
        finally:
            os.close(archive_fd)
        self.assertEqual(installed, self.fixture.paths.releases / "release-one")
        self.assertEqual(stop_calls, [installed])
        self.assertEqual(os.readlink(self.fixture.paths.current), ".releases/release-one")
        for name in release.FACADE_DIRECTORIES:
            self.assertEqual(os.readlink(self.fixture.install / name), f"current/{name}")
            self.assertTrue((self.fixture.paths.legacy_backup / name).is_dir())
        self.assertEqual(updater.active_path(), installed)
        self.assertFalse(self.fixture.paths.bootstrap_journal_file.exists())
        self.assertEqual(before, release._state_tree_fingerprint(self.fixture.persistent))

    def test_bootstrap_backend_accepts_operator_archive_metadata(self) -> None:
        archive = self.fixture.package("release-one", 1)
        hardlink = self.fixture.root / "backend-operator-copy.tar.gz"
        os.link(archive, hardlink)
        os.chmod(archive, 0o666)
        if os.geteuid() == 0:
            os.chown(archive, 65534, 65534)
        expected_manifest, expected_signature, *_ = self._embedded_bootstrap_inputs(
            archive
        )

        archive_fd = os.open(archive, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW)
        try:
            manifest, signature = backend._read_signed_metadata(archive_fd)
        finally:
            os.close(archive_fd)
        self.assertEqual(manifest, expected_manifest)
        self.assertEqual(signature, expected_signature)

    def test_embedded_verifier_bootstrap_health_failure_restores_legacy(self) -> None:
        archive = self.fixture.package("release-one", 1)
        self._create_legacy_layout()
        manifest, signature, public_key, raw_key, key_id = self._embedded_bootstrap_inputs(
            archive
        )
        lifecycle: list[str] = []
        updater = release.ReleaseUpdater(
            self.fixture.paths, lifecycle_callback=lifecycle.append
        )
        updater._run_signed_candidate_stop = lambda _: None
        restarted: list[Path] = []
        updater._run_legacy_restart_process = lambda candidate: restarted.append(candidate)

        def reject_health(*_: object) -> None:
            raise release.ReleaseError("synthetic health rejection")

        updater._run_bootstrap_health_gate = reject_health
        archive_fd = os.open(archive, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW)
        try:
            with self.assertRaises(release.ReleaseError):
                updater.bootstrap_from_embedded_verifier(
                    archive_fd,
                    manifest,
                    signature,
                    public_key,
                    raw_key,
                    key_id,
                    hashlib.sha256(public_key).digest(),
                )
        finally:
            os.close(archive_fd)
        self.assertFalse(self.fixture.paths.state_file.exists())
        self.assertFalse(self.fixture.paths.current.exists())
        self.assertFalse(self.fixture.paths.bootstrap_journal_file.exists())
        self.assertFalse((self.fixture.paths.releases / "release-one").exists())
        self.assertEqual(restarted, [self.fixture.paths.releases / "release-one"])
        self.assertEqual(
            lifecycle,
            [
                "bootstrap_legacy_restart_attempted",
                "bootstrap_legacy_restart_succeeded",
            ],
        )
        for name in release.FACADE_DIRECTORIES:
            self.assertTrue((self.fixture.install / name).is_dir())
            self.assertFalse((self.fixture.install / name).is_symlink())

    def test_each_bootstrap_facade_interruption_recovers_legacy(self) -> None:
        for facade_name in release.FACADE_DIRECTORIES:
            with self.subTest(facade=facade_name):
                nested = Path(self.temporary.name) / f"facade-{facade_name}"
                nested.mkdir()
                fixture = Fixture(nested)
                archive = fixture.package("release-one", 1)
                self._create_legacy_layout(fixture)
                inputs = self._embedded_bootstrap_inputs(archive, fixture)
                interrupted = release.ReleaseUpdater(
                    fixture.paths,
                    failpoint=f"bootstrap_after_facade_{facade_name}",
                )
                interrupted._run_bootstrap_health_gate = lambda *_: None
                interrupted._run_signed_candidate_stop = lambda _: None
                archive_fd = os.open(
                    archive, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW
                )
                try:
                    with self.assertRaises(release.InjectedInterruption):
                        interrupted.bootstrap_from_embedded_verifier(
                            archive_fd,
                            *inputs,
                            hashlib.sha256(inputs[2]).digest(),
                        )
                finally:
                    os.close(archive_fd)
                start_path = fixture.install / "scripts/inte_run_start.sh"
                if facade_name == "scripts":
                    self.assertNotIn(b"legacy-start-marker", start_path.read_bytes())
                recovering = release.ReleaseUpdater(fixture.paths)
                recovering._run_legacy_restart_process = lambda _: None
                self.assertIsNone(recovering.recover_failed_bootstrap())
                self.assertFalse(fixture.paths.current.exists())
                self.assertFalse(fixture.paths.bootstrap_journal_file.exists())
                self.assertIn(b"legacy-start-marker", start_path.read_bytes())
                for name in release.FACADE_DIRECTORIES:
                    self.assertTrue((fixture.install / name).is_dir())
                    self.assertFalse((fixture.install / name).is_symlink())

    def test_embedded_verifier_bootstrap_state_write_is_idempotently_completed(self) -> None:
        archive = self.fixture.package("release-one", 1)
        self._create_legacy_layout()
        manifest, signature, public_key, raw_key, key_id = self._embedded_bootstrap_inputs(
            archive
        )
        updater = release.ReleaseUpdater(
            self.fixture.paths, failpoint="bootstrap_after_state"
        )
        updater._run_bootstrap_health_gate = lambda *_: None
        updater._run_signed_candidate_stop = lambda _: None
        archive_fd = os.open(archive, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW)
        try:
            installed = updater.bootstrap_from_embedded_verifier(
                archive_fd,
                manifest,
                signature,
                public_key,
                raw_key,
                key_id,
                hashlib.sha256(public_key).digest(),
            )
        finally:
            os.close(archive_fd)
        self.assertEqual(installed.name, "release-one")
        self.assertEqual(updater.active_path(), installed)
        self.assertFalse(self.fixture.paths.bootstrap_journal_file.exists())

    def test_embedded_verifier_bootstrap_stop_failure_precedes_all_facade_moves(self) -> None:
        archive = self.fixture.package("release-one", 1)
        self._create_legacy_layout()
        manifest, signature, public_key, raw_key, key_id = self._embedded_bootstrap_inputs(
            archive
        )
        updater = release.ReleaseUpdater(self.fixture.paths)

        def reject_stop(candidate: Path) -> None:
            self.assertEqual(candidate, self.fixture.paths.releases / "release-one")
            self.assertFalse(self.fixture.paths.legacy_backup.exists())
            raise release.ReleaseError("synthetic signed stop rejection")

        updater._run_signed_candidate_stop = reject_stop
        updater._run_bootstrap_health_gate = lambda *_: None
        archive_fd = os.open(archive, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW)
        try:
            with self.assertRaises(release.ReleaseError):
                updater.bootstrap_from_embedded_verifier(
                    archive_fd,
                    manifest,
                    signature,
                    public_key,
                    raw_key,
                    key_id,
                    hashlib.sha256(public_key).digest(),
                )
        finally:
            os.close(archive_fd)
        journal = updater._load_bootstrap_journal()
        self.assertIsNotNone(journal)
        assert journal is not None
        self.assertEqual(journal["phase"], "staged")
        self.assertFalse(self.fixture.paths.current.exists())
        self.assertFalse(self.fixture.paths.legacy_backup.exists())
        for name in release.FACADE_DIRECTORIES:
            self.assertTrue((self.fixture.install / name).is_dir())
            self.assertFalse((self.fixture.install / name).is_symlink())
        updater._run_legacy_restart_process = lambda _: None
        self.assertIsNone(updater.recover_failed_bootstrap())

    def test_embedded_verifier_bootstrap_stopped_phase_failpoint_recovers_legacy(self) -> None:
        archive = self.fixture.package("release-one", 1)
        self._create_legacy_layout()
        manifest, signature, public_key, raw_key, key_id = self._embedded_bootstrap_inputs(
            archive
        )
        updater = release.ReleaseUpdater(
            self.fixture.paths, failpoint="bootstrap_after_stop"
        )
        stop_calls: list[Path] = []
        updater._run_signed_candidate_stop = lambda candidate: stop_calls.append(candidate)
        updater._run_bootstrap_health_gate = lambda *_: None
        archive_fd = os.open(archive, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW)
        try:
            with self.assertRaises(release.InjectedInterruption):
                updater.bootstrap_from_embedded_verifier(
                    archive_fd,
                    manifest,
                    signature,
                    public_key,
                    raw_key,
                    key_id,
                    hashlib.sha256(public_key).digest(),
                )
        finally:
            os.close(archive_fd)
        self.assertEqual(
            stop_calls, [self.fixture.paths.releases / "release-one"]
        )
        journal = updater._load_bootstrap_journal()
        self.assertIsNotNone(journal)
        assert journal is not None
        self.assertEqual(journal["phase"], "stopped")
        self.assertFalse(self.fixture.paths.legacy_backup.exists())
        recovering = release.ReleaseUpdater(self.fixture.paths)
        recovering._run_legacy_restart_process = lambda _: None
        self.assertIsNone(recovering.recover_failed_bootstrap())
        self.assertFalse(self.fixture.paths.bootstrap_journal_file.exists())
        self.assertFalse((self.fixture.paths.releases / "release-one").exists())
        for name in release.FACADE_DIRECTORIES:
            self.assertTrue((self.fixture.install / name).is_dir())
            self.assertFalse((self.fixture.install / name).is_symlink())

    def test_bootstrap_recovery_restart_failure_remains_fail_closed(self) -> None:
        archive = self.fixture.package("release-one", 1)
        self._create_legacy_layout()
        manifest, signature, public_key, raw_key, key_id = self._embedded_bootstrap_inputs(
            archive
        )
        interrupted = release.ReleaseUpdater(
            self.fixture.paths, failpoint="bootstrap_after_stop"
        )
        interrupted._run_signed_candidate_stop = lambda _: None
        interrupted._run_bootstrap_health_gate = lambda *_: None
        archive_fd = os.open(archive, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW)
        try:
            with self.assertRaises(release.InjectedInterruption):
                interrupted.bootstrap_from_embedded_verifier(
                    archive_fd,
                    manifest,
                    signature,
                    public_key,
                    raw_key,
                    key_id,
                    hashlib.sha256(public_key).digest(),
                )
        finally:
            os.close(archive_fd)

        lifecycle: list[str] = []
        recovering = release.ReleaseUpdater(
            self.fixture.paths, lifecycle_callback=lifecycle.append
        )

        def reject_restart(_: Path) -> None:
            raise release.ReleaseError("synthetic legacy restart rejection")

        recovering._run_legacy_restart_process = reject_restart
        with self.assertRaises(release.ReleaseError):
            recovering.recover_failed_bootstrap()
        self.assertEqual(lifecycle, ["bootstrap_legacy_restart_attempted"])
        self.assertTrue(self.fixture.paths.bootstrap_journal_file.exists())
        self.assertTrue((self.fixture.paths.releases / "release-one").is_dir())

        retry = release.ReleaseUpdater(self.fixture.paths)
        retry._run_legacy_restart_process = lambda _: None
        self.assertIsNone(retry.recover_failed_bootstrap())
        self.assertFalse(self.fixture.paths.bootstrap_journal_file.exists())
        self.assertFalse((self.fixture.paths.releases / "release-one").exists())

    def test_bootstrap_health_uses_stable_evaluator_and_stops_probe(self) -> None:
        updater = release.ReleaseUpdater(self.fixture.paths)
        updater._initialize_directories()
        candidate = self.fixture.paths.releases / "release-one"
        shutil.copytree(
            self.fixture.payload(
                "health-probe",
                health_script_body=b"#!/bin/sh\nexit 0\n",
            ),
            candidate,
            symlinks=True,
        )
        plan = updater._preflight_bootstrap_health_gate(candidate)
        self.assertEqual(plan.run_script, candidate / "scripts/run_start.sh")
        self.assertEqual(plan.health_script, self.fixture.paths.stable_health_script)
        self.assertEqual(plan.stop_script, candidate / "scripts/stop.sh")
        self.assertEqual(plan.log_path.parent, self.fixture.paths.state_dir)
        self.assertEqual(stat.S_IMODE(os.lstat(plan.log_path).st_mode), 0o600)

        class Probe:
            pid = 4242
            stopped = False

            def poll(self) -> int | None:
                return 0 if self.stopped else None

            def wait(self, timeout: int | None = None) -> int:
                if not self.stopped:
                    raise subprocess.TimeoutExpired("probe", timeout)
                return 0

        probe = Probe()
        stop_calls: list[Path] = []

        def stop_probe(release_path: Path) -> None:
            stop_calls.append(release_path)
            probe.stopped = True

        updater._run_signed_candidate_stop = stop_probe
        healthy = subprocess.CompletedProcess(args=[str(plan.health_script)], returncode=0)
        evaluated_scripts: list[bytes] = []

        def run_health(
            arguments: Sequence[str], **keywords: object
        ) -> subprocess.CompletedProcess[bytes]:
            health_path = Path(arguments[0])
            evaluated_scripts.append(health_path.read_bytes())
            self.assertEqual(keywords["pass_fds"], (int(health_path.name),))
            return healthy

        with mock.patch.object(release.subprocess, "Popen", return_value=probe), mock.patch.object(
            release.subprocess, "run", side_effect=run_health
        ):
            updater._run_bootstrap_health_gate(candidate, plan)
        self.assertEqual(stop_calls, [candidate])
        self.assertEqual(probe.poll(), 0)
        self.assertEqual(
            evaluated_scripts,
            [self.fixture.paths.stable_health_script.read_bytes()],
        )
        self.assertNotEqual(
            evaluated_scripts[0],
            (candidate / "scripts/release_health_check.sh").read_bytes(),
        )

        os.chmod(candidate / "scripts/stop.sh", 0o775)
        updater._preflight_bootstrap_health_gate(candidate)

    def test_model_guard_fingerprint_only_tracks_device_certificate(self) -> None:
        certificate = self.fixture.persistent / "device-certificate.bin"
        baseline = release._state_tree_fingerprint(self.fixture.persistent)

        os.chmod(self.fixture.persistent, 0o755)
        os.chmod(certificate, 0o644)
        os.link(certificate, self.fixture.persistent / "certificate-hardlink")
        write(
            self.fixture.persistent / "ignored-state.bin",
            b"not part of the device authorization state",
            0o666,
        )
        (self.fixture.persistent / "ignored-directory").mkdir(mode=0o777)
        self.assertEqual(
            baseline,
            release._state_tree_fingerprint(self.fixture.persistent),
        )

        certificate.write_bytes(b"CMDC-replaced-device-certificate")
        self.assertNotEqual(
            baseline,
            release._state_tree_fingerprint(self.fixture.persistent),
        )

    def test_model_guard_fingerprint_accepts_missing_certificate(self) -> None:
        certificate = self.fixture.persistent / "device-certificate.bin"
        certificate.unlink()
        empty_root = release._state_tree_fingerprint(self.fixture.persistent)
        self.assertEqual(
            empty_root,
            release._state_tree_fingerprint(self.fixture.persistent),
        )
        self.fixture.persistent.rmdir()
        self.assertIsInstance(
            release._state_tree_fingerprint(self.fixture.persistent),
            str,
        )

    def test_model_guard_fingerprint_accepts_symlink_certificate(self) -> None:
        certificate = self.fixture.persistent / "device-certificate.bin"
        baseline = release._state_tree_fingerprint(self.fixture.persistent)
        target = self.fixture.root / "replacement-certificate.bin"
        target.write_bytes(certificate.read_bytes())
        certificate.unlink()
        certificate.symlink_to(target)
        self.assertEqual(
            release._state_tree_fingerprint(self.fixture.persistent),
            baseline,
        )

    def test_backend_recovery_reports_committed_and_legacy_restored(self) -> None:
        committed = backend.INSTALL_ROOT / ".releases/release-one"
        self.assertEqual(backend._recovery_result(True, committed), str(committed))
        self.assertEqual(backend._recovery_result(True, None), "legacy-restored")
        with self.assertRaises(backend.BackendError):
            backend._recovery_result(False, None)

    def test_signed_update_commits_through_journal_and_preserves_device_state(self) -> None:
        first = self.fixture.package("release-one", 1)
        updater = self.fixture.bootstrap(first)
        before = release._state_tree_fingerprint(self.fixture.persistent)
        second = self.fixture.package("release-two", 2)
        manifest = updater.prepare(second)
        self.assertEqual(manifest["release_id"], "release-two")
        self.assertEqual(os.readlink(self.fixture.paths.current), ".releases/release-one")
        candidate = updater.activate()
        self.assertEqual(candidate.name, "release-two")
        self.assertEqual(os.readlink(self.fixture.paths.current), ".releases/release-two")
        marker = candidate / release.PUBLICATION_MARKER_PATH
        self.assertEqual(stat.S_IMODE(os.lstat(marker).st_mode), 0o600)
        self.assertEqual(
            hashlib.sha256(marker.read_bytes()).hexdigest(),
            updater._load_journal()["publication_marker_sha256"],
        )
        updater.commit_healthy()
        self.assertEqual(updater.active_path().name, "release-two")
        self.assertEqual(before, release._state_tree_fingerprint(self.fixture.persistent))

    def test_old_and_mixed_release_schemas_are_rejected(
        self,
    ) -> None:
        current = self.fixture.package("release-one-v3", 1)
        updater = self.fixture.bootstrap(current)
        incoming = self.fixture.package("release-two-source", 2)
        with tarfile.open(incoming, "r:gz") as bundle:
            stream = bundle.extractfile("meta/compatibility.manifest.json")
            assert stream is not None
            v3_manifest = json.loads(stream.read())
        old_manifest = json.loads(json.dumps(v3_manifest))
        old_manifest["format"] = "cosmo-release-compatibility-v2"
        old_bytes, old_signature = self.fixture.sign_release_manifest(
            old_manifest
        )

        downgraded = self.fixture.root / "incoming-v2.tar.gz"

        def replace_manifest(members):
            for index, (member, data) in enumerate(members):
                if member.name == "meta/compatibility.manifest.json":
                    member.size = len(old_bytes)
                    members[index] = (member, old_bytes)
                elif member.name == "meta/compatibility.manifest.sig":
                    member.size = len(old_signature)
                    members[index] = (member, old_signature)

        rewrite_archive(incoming, downgraded, replace_manifest)
        with self.assertRaises(release.ReleaseError):
            updater.prepare(downgraded)

        mixed_v1 = json.loads(json.dumps(v3_manifest))
        mixed_v1["format"] = "cosmo-release-compatibility-v1"
        with self.assertRaises(release.ReleaseError):
            release._validate_active_compatibility_manifest(mixed_v1)

        missing_v3 = json.loads(json.dumps(v3_manifest))
        missing_v3.pop("device_certificate_schema")
        with self.assertRaises(release.ReleaseError):
            release._validate_compatibility_manifest(missing_v3)

    def test_duplicate_historical_release_id_is_rejected_before_journal(self) -> None:
        first = self.fixture.package("release-one", 1)
        updater = self.fixture.bootstrap(first)
        updater.prepare(self.fixture.package("release-two", 2))
        updater.activate()
        updater.commit_healthy()
        historical = self.fixture.paths.releases / "release-one"
        self.assertTrue(historical.is_dir())

        shutil.rmtree(self.fixture.root / "payload-release-one")
        first.unlink()
        duplicate = self.fixture.package("release-one", 3)
        stop = mock.Mock(side_effect=AssertionError("collision must not execute stop"))
        updater._run_signed_candidate_stop = stop
        with self.assertRaisesRegex(release.ReleaseError, "release ID already exists"):
            updater.prepare(duplicate)

        stop.assert_not_called()
        self.assertFalse(self.fixture.paths.journal_file.exists())
        self.assertTrue(historical.is_dir())
        self.assertEqual(updater.recover().name, "release-two")

    def test_preset_model_uses_regular_data_mode(self) -> None:
        archive = self.fixture.package("release-one", 1)
        with tarfile.open(archive, "r:gz") as bundle:
            payload_stream = bundle.extractfile("meta/payload.files.json")
            self.assertIsNotNone(payload_stream)
            assert payload_stream is not None
            payload_manifest = json.loads(payload_stream.read())
            entry = next(
                item
                for item in payload_manifest["entries"]
                if item["path"] == "resource/models/preset-one/model.nn"
            )
            self.assertEqual(entry["mode"], 0o644)
            self.assertEqual(
                stat.S_IMODE(
                    bundle.getmember("payload/resource/models/preset-one/model.nn").mode
                ),
                0o644,
            )
        updater = self.fixture.bootstrap(archive)
        installed = updater.active_path() / "resource/models/preset-one/model.nn"
        self.assertEqual(stat.S_IMODE(os.lstat(installed).st_mode), 0o644)

    def test_payload_schema_ignores_file_modes(self) -> None:
        archive = self.fixture.package("release-one", 1)
        with tarfile.open(archive, "r:gz") as bundle:
            payload_stream = bundle.extractfile("meta/payload.files.json")
            assert payload_stream is not None
            payload_manifest = json.loads(payload_stream.read())

        read_only_preset = json.loads(json.dumps(payload_manifest))
        preset_entry = next(
            item
            for item in read_only_preset["entries"]
            if item["path"] == "resource/models/preset-one/model.nn"
        )
        preset_entry["mode"] = 0o444
        release._validate_payload_manifest(read_only_preset)

        read_only_nonpreset = json.loads(json.dumps(payload_manifest))
        runtime_entry = next(
            item
            for item in read_only_nonpreset["entries"]
            if item["path"] == "lib/libbmrt.so"
        )
        runtime_entry["mode"] = 0o444
        release._validate_payload_manifest(read_only_nonpreset)

    def test_preset_scanner_ignores_mode_and_link_count(self) -> None:
        root = self.fixture.root / "writable-preset-release"
        model = root / "resource/models/preset-one/model.nn"
        write(model, CEM_V2_GOLDEN_CORE, 0o644)
        alias = model.with_name("model-alias.nn")
        os.link(model, alias)
        snapshots = release._scan_preset_models(root)
        self.assertEqual(len(snapshots), 1)
        self.assertEqual(snapshots[0].core_sha256, CEM_V2_GOLDEN_SHA256)

    def test_preset_scanner_accepts_model_symlink(self) -> None:
        root = self.fixture.root / "symlink-preset-release"
        target = root / "shared/preset-one.cemc"
        write(target, CEM_V2_GOLDEN_CORE, 0o644)
        model = root / "resource/models/preset-one/model.nn"
        model.parent.mkdir(parents=True)
        model.symlink_to(target)
        snapshots = release._scan_preset_models(root)
        self.assertEqual(len(snapshots), 1)
        self.assertEqual(snapshots[0].core_sha256, CEM_V2_GOLDEN_SHA256)

    def test_preset_scanner_accepts_model_guard_official_golden_core(self) -> None:
        self.assertEqual(
            hashlib.sha256(CEM_V2_GOLDEN_CORE).hexdigest(),
            CEM_V2_GOLDEN_SHA256,
        )
        root = self.fixture.root / "golden-cem-v2-release"
        model = root / "resource/models/preset-one/model.nn"
        write(model, CEM_V2_GOLDEN_CORE, 0o444)
        snapshots = release._scan_preset_models(root)
        self.assertEqual(len(snapshots), 1)
        self.assertEqual(snapshots[0].model_id, "test_model_001")

    def test_preset_scanner_and_packager_reject_mixed_cohorts(self) -> None:
        payload = self.fixture.payload("mixed-preset-cohorts")
        os.chmod(
            payload / "resource/models/preset-one/model.nn",
            0o444,
        )
        second_model = (
            payload / "resource/models/preset-two/model.nn"
        )
        write(
            second_model,
            cem_v2_core_with_cohort(bytes.fromhex("30" * 16)),
            0o444,
        )

        with self.assertRaisesRegex(
            release.ReleaseError,
            "preset models use mixed cohort IDs",
        ):
            release._scan_preset_models(payload)
        with self.assertRaisesRegex(
            RuntimeError,
            "preset models use mixed cohort IDs",
        ):
            self.fixture.package_existing(
                payload,
                "mixed-preset-cohorts",
                1,
            )

    def test_preset_scanner_rejects_zero_cohort(self) -> None:
        root = self.fixture.root / "zero-preset-cohort"
        model = root / "resource/models/preset-one/model.nn"
        write(model, cem_v2_core_with_cohort(bytes(16)), 0o444)
        with self.assertRaisesRegex(
            release.ReleaseError,
            "preamble cohort ID must be nonzero",
        ):
            release._scan_preset_models(root)

    def test_payload_rejects_device_certificate(self) -> None:
        payload = self.fixture.payload("device-certificate")
        write(
            payload
            / "share/cosmo-model-guard/device-certificate.bin",
            b"CMDC-device-specific",
            0o600,
        )
        with self.assertRaisesRegex(
            RuntimeError,
            "device-specific Model Guard state",
        ):
            self.fixture.package_existing(
                payload,
                "device-certificate",
                1,
            )

    def test_preset_scanner_rejects_cemc_prefix_and_truncations(self) -> None:
        root = self.fixture.root / "malformed-cem-v2-release"
        model = root / "resource/models/preset-one/model.nn"
        malformed = (
            b"CEMC" + b"\x01\x00\x01\xec" + b"plaintext-bmodel",
            CEM_V2_GOLDEN_CORE[:4],
            CEM_V2_GOLDEN_CORE[:111],
            CEM_V2_GOLDEN_CORE[:112],
            CEM_V2_GOLDEN_CORE[:-1],
        )
        for index, core in enumerate(malformed):
            with self.subTest(index=index, size=len(core)):
                write(model, core, 0o444)
                with self.assertRaisesRegex(
                    release.ReleaseError, "invalid CEM v2 preset"
                ):
                    release._scan_preset_models(root)

    def test_preset_scanner_rejects_manifest_hash_mismatch(self) -> None:
        root = self.fixture.root / "cem-v2-hash-release"
        model = root / "resource/models/preset-one/model.nn"
        core = bytearray(CEM_V2_GOLDEN_CORE)
        core[112 + 20] ^= 1
        write(model, bytes(core), 0o444)
        with self.assertRaisesRegex(release.ReleaseError, "manifest digest mismatch"):
            release._scan_preset_models(root)

    def test_preset_scanner_rejects_noncanonical_cbor(self) -> None:
        root = self.fixture.root / "cem-v2-cbor-release"
        model = root / "resource/models/preset-one/model.nn"
        canonical = cem_v2_golden_manifest()
        mutations = {
            "non-minimal integer": canonical[:2] + b"\x18\x01" + canonical[3:],
            "wrong key order": canonical[:1] + b"\x02" + canonical[2:],
            "trailing item": canonical + b"\x00",
        }
        for name, manifest in mutations.items():
            with self.subTest(name=name):
                write(model, cem_v2_core_with_manifest(manifest), 0o444)
                with self.assertRaisesRegex(
                    release.ReleaseError, "invalid CEM v2 preset"
                ):
                    release._scan_preset_models(root)

    def test_preset_scanner_rejects_inconsistent_chunk_layout(self) -> None:
        root = self.fixture.root / "cem-v2-layout-release"
        model = root / "resource/models/preset-one/model.nn"
        manifest = bytearray(cem_v2_golden_manifest())
        descriptor = b"\x0f\x81\x85\x00\x00\x14\x00\x18\x24"
        descriptor_offset = manifest.index(descriptor)
        manifest[descriptor_offset + 6] = 1
        write(model, cem_v2_core_with_manifest(bytes(manifest)), 0o444)
        with self.assertRaisesRegex(release.ReleaseError, "chunk layout"):
            release._scan_preset_models(root)

    def test_health_failure_rolls_back_and_preserves_identity(self) -> None:
        updater = self.fixture.bootstrap(self.fixture.package("release-one", 1))
        before = release._state_tree_fingerprint(self.fixture.persistent)
        updater.prepare(self.fixture.package("release-two", 2))
        candidate = updater.activate()
        stop_calls: list[Path] = []
        updater._run_signed_candidate_stop = lambda path: stop_calls.append(path)
        restored = updater.rollback()
        self.assertEqual(stop_calls, [candidate])
        self.assertEqual(restored.name, "release-one")
        self.assertEqual(os.readlink(self.fixture.paths.current), ".releases/release-one")
        self.assertFalse((self.fixture.paths.releases / "release-two").exists())
        self.assertEqual(before, release._state_tree_fingerprint(self.fixture.persistent))

    def test_pending_health_script_is_resolved_from_trusted_active_release(self) -> None:
        updater = self.fixture.bootstrap(self.fixture.package("release-one", 1))
        updater.prepare(self.fixture.package("release-two", 2))
        candidate = updater.activate()
        expected = (
            self.fixture.paths.releases
            / "release-one"
            / "scripts/release_health_check.sh"
        )
        self.assertEqual(updater.pending_health_script(), expected)
        with self.assertRaises(release.ReleaseError):
            updater._validate_signed_candidate_script(
                candidate, "../../release-two/scripts/stop.sh"
            )

        os.chmod(expected, 0o775)
        self.assertEqual(updater.pending_health_script(), expected)
        os.chmod(expected, 0o755)

        scripts = expected.parent
        os.chmod(scripts, 0o775)
        self.assertEqual(updater.pending_health_script(), expected)

    def test_start_contract_invokes_incoming_health_through_trusted_updater(self) -> None:
        start = (REPOSITORY / "scripts/start.sh").read_text(encoding="utf-8")
        self.assertIn('"${SCRIPT_DIR}/install.sh" pending-health-script', start)
        self.assertIn('"${SCRIPT_DIR}/install.sh" run-pending-health', start)
        self.assertIn('"$runner_pid" "$release_root"', start)
        self.assertNotIn(
            '"$candidate_health_script" "$runner_pid"', start
        )
        self.assertNotRegex(start, r"stop\.sh[^\n]*\|\|\s*true")
        self.assertEqual(
            start.count(
                'for candidate in "${COSMO_UPGRADE_DIR}"/*.tar.gz; do'
            ),
            1,
        )
        self.assertEqual(start.count("find_signed_release_archive"), 3)
        self.assertNotRegex(start, r"\$\(\s*find_signed_release_archive")
        self.assertNotIn("non-canonical name", start)

    def test_start_discovers_any_tar_gz_name(self) -> None:
        start = (REPOSITORY / "scripts/start.sh").read_text(encoding="utf-8")
        self.assertIn(
            'for candidate in "${COSMO_UPGRADE_DIR}"/*.tar.gz; do',
            start,
        )
        self.assertNotIn("filename=", start)

    def test_trusted_updater_times_out_hanging_incoming_health(self) -> None:
        trusted = self.fixture.package(
            "release-one",
            1,
            health_script_body=b"#!/bin/sh\nsleep 5\n",
        )
        updater = self.fixture.bootstrap(trusted)
        updater.prepare(self.fixture.package("release-two", 2))
        candidate = updater.activate()

        with mock.patch.object(release, "CANDIDATE_HEALTH_TIMEOUT_SECONDS", 0.1):
            with self.assertRaisesRegex(release.ReleaseError, "trusted 0.1s timeout"):
                updater.run_pending_health(str(os.getpid()), candidate)

        self.assertEqual(os.readlink(self.fixture.paths.current), ".releases/release-two")
        self.assertTrue(self.fixture.paths.journal_file.is_file())
        stop = mock.Mock()
        updater._run_signed_candidate_stop = stop
        self.assertEqual(updater.rollback().name, "release-one")
        stop.assert_called_once_with(candidate)

    def test_incoming_exit_zero_cannot_bypass_trusted_health_rejection(self) -> None:
        trusted = self.fixture.package(
            "release-one",
            1,
            health_script_body=b"#!/bin/sh\nexit 23\n",
        )
        updater = self.fixture.bootstrap(trusted)
        incoming = self.fixture.package(
            "release-two",
            2,
            health_script_body=b"#!/bin/sh\nexit 0\n",
        )
        updater.prepare(incoming)
        candidate = updater.activate()

        with self.assertRaisesRegex(
            release.ReleaseError,
            "trusted active-release health check",
        ):
            updater.run_pending_health(str(os.getpid()), candidate)

        self.assertEqual(
            os.readlink(self.fixture.paths.current),
            ".releases/release-two",
        )
        self.assertTrue(self.fixture.paths.journal_file.is_file())

    def test_rollback_stop_failure_keeps_pointer_journal_and_incoming_tree(self) -> None:
        updater = self.fixture.bootstrap(self.fixture.package("release-one", 1))
        updater.prepare(self.fixture.package("release-two", 2))
        candidate = updater.activate()

        def reject_stop(_: Path) -> None:
            raise release.ReleaseError("synthetic candidate stop failure")

        updater._run_signed_candidate_stop = reject_stop
        with self.assertRaises(release.ReleaseError):
            updater.rollback()
        self.assertEqual(os.readlink(self.fixture.paths.current), ".releases/release-two")
        self.assertTrue(self.fixture.paths.journal_file.is_file())
        self.assertTrue(candidate.is_dir())
        self.assertEqual(updater._load_state()["current_release_id"], "release-one")

    def test_recover_stop_failure_keeps_pointer_journal_and_incoming_tree(self) -> None:
        updater = self.fixture.bootstrap(self.fixture.package("release-one", 1))
        updater.prepare(self.fixture.package("release-two", 2))
        candidate = updater.activate()
        recovering = release.ReleaseUpdater(self.fixture.paths)

        def reject_stop(_: Path) -> None:
            raise release.ReleaseError("synthetic candidate stop failure")

        recovering._run_signed_candidate_stop = reject_stop
        with self.assertRaises(release.ReleaseError):
            recovering.recover()
        self.assertEqual(os.readlink(self.fixture.paths.current), ".releases/release-two")
        self.assertTrue(self.fixture.paths.journal_file.is_file())
        self.assertTrue(candidate.is_dir())

    def test_recover_stops_candidate_before_reversing_and_removing_tree(self) -> None:
        updater = self.fixture.bootstrap(self.fixture.package("release-one", 1))
        updater.prepare(self.fixture.package("release-two", 2))
        candidate = updater.activate()
        recovering = release.ReleaseUpdater(self.fixture.paths)
        stop_observations: list[tuple[Path, str, bool, bool]] = []

        def observe_stop(path: Path) -> None:
            stop_observations.append(
                (
                    path,
                    os.readlink(self.fixture.paths.current),
                    self.fixture.paths.journal_file.is_file(),
                    path.is_dir(),
                )
            )

        recovering._run_signed_candidate_stop = observe_stop
        restored = recovering.recover()
        self.assertEqual(
            stop_observations,
            [(candidate, ".releases/release-two", True, True)],
        )
        self.assertEqual(restored.name, "release-one")
        self.assertEqual(os.readlink(self.fixture.paths.current), ".releases/release-one")
        self.assertFalse(self.fixture.paths.journal_file.exists())
        self.assertFalse(candidate.exists())

    def test_every_transaction_failpoint_recovers_previous_release(self) -> None:
        for failpoint in (
            "after_journal",
            "after_extract",
            "after_publication_marker",
            "before_release_publish",
            "after_release_rename",
            "after_release_publish",
            "after_switch",
        ):
            with self.subTest(failpoint=failpoint):
                nested = Path(self.temporary.name) / failpoint
                nested.mkdir()
                fixture = Fixture(nested)
                fixture.bootstrap(fixture.package("release-one", 1))
                candidate = fixture.package("release-two", 2)
                interrupted = release.ReleaseUpdater(fixture.paths, failpoint=failpoint)
                with self.assertRaises(release.InjectedInterruption):
                    if failpoint == "after_switch":
                        interrupted.prepare(candidate)
                        interrupted.activate()
                    else:
                        interrupted.prepare(candidate)
                recovering = release.ReleaseUpdater(fixture.paths)
                stop_calls: list[Path] = []
                recovering._run_signed_candidate_stop = lambda path: stop_calls.append(path)
                recovered = recovering.recover()
                self.assertEqual(recovered.name, "release-one")
                self.assertEqual(os.readlink(fixture.paths.current), ".releases/release-one")
                self.assertFalse(fixture.paths.journal_file.exists())
                expected_stops = (
                    [fixture.paths.releases / "release-two"]
                    if failpoint == "after_switch"
                    else []
                )
                self.assertEqual(stop_calls, expected_stops)

    def test_recovery_preserves_unowned_tree_racing_publication(self) -> None:
        updater = self.fixture.bootstrap(self.fixture.package("release-one", 1))
        interrupted = release.ReleaseUpdater(
            self.fixture.paths, failpoint="before_release_publish"
        )
        with self.assertRaises(release.InjectedInterruption):
            interrupted.prepare(self.fixture.package("release-two", 2))
        journal = interrupted._load_journal()
        self.assertIsNotNone(journal)
        assert journal is not None
        self.assertEqual(journal["phase"], "publishing")

        collision = self.fixture.paths.releases / "release-two"
        collision.mkdir(mode=0o755)
        write(collision / "sentinel", b"must-survive", 0o600)
        recovering = release.ReleaseUpdater(self.fixture.paths)
        stop = mock.Mock(side_effect=AssertionError("unowned tree must not execute stop"))
        recovering._run_signed_candidate_stop = stop
        with self.assertRaises(release.ReleaseError):
            recovering.recover()

        stop.assert_not_called()
        self.assertEqual((collision / "sentinel").read_bytes(), b"must-survive")
        self.assertTrue(self.fixture.paths.journal_file.is_file())

    def test_state_commit_failpoint_is_idempotently_committed_by_recovery_and_rollback(self) -> None:
        for operation in ("recover", "rollback"):
            with self.subTest(operation=operation):
                nested = Path(self.temporary.name) / f"state-commit-{operation}"
                nested.mkdir()
                fixture = Fixture(nested)
                fixture.bootstrap(fixture.package("release-one", 1))
                interrupted = release.ReleaseUpdater(
                    fixture.paths, failpoint="after_state_commit"
                )
                interrupted.prepare(fixture.package("release-two", 2))
                interrupted.activate()
                with self.assertRaises(release.InjectedInterruption):
                    interrupted.commit_healthy()
                self.assertTrue(fixture.paths.journal_file.exists())
                durable_state = release._strict_json(
                    fixture.paths.state_file.read_bytes(),
                    release.MAX_MANIFEST_BYTES,
                    "test state",
                )
                self.assertEqual(durable_state["current_release_id"], "release-two")

                updater = release.ReleaseUpdater(fixture.paths)
                completed = getattr(updater, operation)()
                self.assertEqual(completed.name, "release-two")
                self.assertEqual(os.readlink(fixture.paths.current), ".releases/release-two")
                self.assertFalse(fixture.paths.journal_file.exists())
                self.assertEqual(updater.recover().name, "release-two")
                self.assertEqual(updater.rollback().name, "release-two")

    def test_manifest_bitflip_is_rejected_before_switch(self) -> None:
        updater = self.fixture.bootstrap(self.fixture.package("release-one", 1))
        second = self.fixture.package("release-two", 2)
        changed = self.fixture.root / "cosmo-release-bitflip.tar.gz"

        def mutate(members):
            for index, (member, data) in enumerate(members):
                if member.name == "meta/compatibility.manifest.json":
                    assert data is not None
                    altered = bytearray(data)
                    altered[data.index(b"release-two")] ^= 1
                    members[index] = (member, bytes(altered))
                    return
            raise AssertionError("manifest not found")

        rewrite_archive(second, changed, mutate)
        with self.assertRaises(release.ReleaseError):
            updater.prepare(changed)
        self.assertEqual(updater.active_path().name, "release-one")

    def test_rejected_archive_before_journal_leaves_no_transaction_orphan(self) -> None:
        updater = self.fixture.bootstrap(self.fixture.package("release-one", 1))
        orphan = self.fixture.paths.transactions / ("a" * 32)
        orphan.mkdir(mode=0o700)
        write(orphan / "signed-release.tar.gz", b"interrupted-copy", 0o600)
        invalid = self.fixture.root / "cosmo-release-invalid.tar.gz"
        write(invalid, b"not-a-release-archive", 0o600)
        with self.assertRaises(release.ReleaseError):
            updater.prepare(invalid)
        self.assertFalse(self.fixture.paths.journal_file.exists())
        self.assertEqual(list(self.fixture.paths.transactions.iterdir()), [])

    def test_unrecognized_unjournaled_transaction_is_not_deleted(self) -> None:
        updater = self.fixture.bootstrap(self.fixture.package("release-one", 1))
        unexpected = self.fixture.paths.transactions / "unexpected"
        unexpected.mkdir(mode=0o700)
        incoming = self.fixture.package("release-two", 2)
        with self.assertRaises(release.ReleaseError):
            updater.prepare(incoming)
        self.assertTrue(unexpected.is_dir())

    def test_prepare_inspects_and_extracts_only_private_archive_copy(self) -> None:
        updater = self.fixture.bootstrap(self.fixture.package("release-one", 1))
        incoming = self.fixture.package("release-two", 2)
        inspect_archive = updater._inspect_archive

        def inspect_private(archive, state, current_release, current_manifest):
            self.assertNotEqual(archive, incoming)
            self.assertEqual(archive.name, "signed-release.tar.gz")
            self.assertEqual(archive.parent.parent, self.fixture.paths.transactions)
            replacement = self.fixture.root / "replacement-invalid-archive"
            write(replacement, b"replaced-after-private-copy", 0o600)
            os.replace(replacement, incoming)
            return inspect_archive(archive, state, current_release, current_manifest)

        updater._inspect_archive = inspect_private
        manifest = updater.prepare(incoming)
        self.assertEqual(manifest["release_id"], "release-two")

    def test_prepare_accepts_operator_owned_writable_hardlinked_archive(self) -> None:
        updater = self.fixture.bootstrap(self.fixture.package("release-one", 1))
        incoming = self.fixture.package("release-two", 2)
        hardlink = self.fixture.root / "operator-upload-copy.tar.gz"
        os.link(incoming, hardlink)
        os.chmod(incoming, 0o666)
        if os.geteuid() == 0:
            os.chown(incoming, 65534, 65534)

        manifest = updater.prepare(incoming)
        self.assertEqual(manifest["release_id"], "release-two")

    def test_extract_revalidates_exact_archive_member_set(self) -> None:
        updater = self.fixture.bootstrap(self.fixture.package("release-one", 1))
        incoming = self.fixture.package("release-two", 2)
        extract_archive = updater._extract_archive

        def extract_changed(archive, inspection, destination):
            changed = archive.parent / "changed-release.tar.gz"

            def add_member(members):
                info = tarfile.TarInfo("payload/unmanifested")
                info.type = tarfile.REGTYPE
                info.mode = 0o644
                info.size = 1
                members.append((info, b"x"))

            rewrite_archive(archive, changed, add_member)
            os.replace(changed, archive)
            return extract_archive(archive, inspection, destination)

        updater._extract_archive = extract_changed
        with self.assertRaises(release.ReleaseError):
            updater.prepare(incoming)
        self.assertTrue(self.fixture.paths.journal_file.exists())
        self.assertEqual(updater.recover().name, "release-one")
        self.assertEqual(list(self.fixture.paths.transactions.iterdir()), [])

    def test_signed_generation_rollback_is_rejected(self) -> None:
        updater = self.fixture.bootstrap(self.fixture.package("release-one", 2))
        rollback = self.fixture.package("release-old", 1)
        with self.assertRaises(release.ReleaseError):
            updater.prepare(rollback)

    def test_payload_bitflip_is_rejected_before_release_publication(self) -> None:
        updater = self.fixture.bootstrap(self.fixture.package("release-one", 1))
        second = self.fixture.package("release-two", 2)
        changed = self.fixture.root / "cosmo-release-payload-bitflip.tar.gz"

        def mutate(members):
            for index, (member, data) in enumerate(members):
                if member.name == "payload/lib/libbmrt.so":
                    assert data is not None
                    altered = bytearray(data)
                    altered[0] ^= 1
                    members[index] = (member, bytes(altered))
                    return
            raise AssertionError("runtime library not found")

        rewrite_archive(second, changed, mutate)
        with self.assertRaises(release.ReleaseError):
            updater.prepare(changed)
        self.assertEqual(os.readlink(self.fixture.paths.current), ".releases/release-one")

    def test_atomic_facade_replacement_is_rejected(self) -> None:
        updater = self.fixture.bootstrap(self.fixture.package("release-one", 1))
        facade = self.fixture.install / "bin"
        facade.unlink()
        facade.symlink_to("current/lib")
        with self.assertRaises(release.ReleaseError):
            updater.active_path()

    def test_archive_path_traversal_hardlink_and_unsafe_symlink_are_rejected(self) -> None:
        updater = self.fixture.bootstrap(self.fixture.package("release-one", 1))
        second = self.fixture.package("release-two", 2)
        cases = {}

        def traversal(members):
            info = tarfile.TarInfo("payload/../escape")
            info.type = tarfile.REGTYPE
            info.mode = 0o644
            info.size = 1
            members.append((info, b"x"))

        def hardlink(members):
            info = tarfile.TarInfo("payload/extra-hardlink")
            info.type = tarfile.LNKTYPE
            info.mode = 0o644
            info.linkname = "payload/bin/cosmo-engine"
            members.append((info, None))

        def unsafe_symlink(members):
            info = tarfile.TarInfo("payload/extra-symlink")
            info.type = tarfile.SYMTYPE
            info.mode = 0o777
            info.linkname = "../outside"
            members.append((info, None))

        cases["traversal"] = traversal
        cases["hardlink"] = hardlink
        cases["unsafe-symlink"] = unsafe_symlink
        for name, mutation in cases.items():
            with self.subTest(case=name):
                changed = self.fixture.root / f"cosmo-release-{name}.tar.gz"
                rewrite_archive(second, changed, mutation)
                with self.assertRaises(release.ReleaseError):
                    updater.prepare(changed)

    def test_archive_final_symlink_is_accepted(self) -> None:
        updater = self.fixture.bootstrap(self.fixture.package("release-one", 1))
        second = self.fixture.package("release-two", 2)
        linked = self.fixture.root / "cosmo-release-linked.tar.gz"
        linked.symlink_to(second.name)
        manifest = updater.prepare(linked)
        self.assertEqual(manifest["release_id"], "release-two")

    def test_current_trust_anchor_accepts_symlink_and_hardlink(self) -> None:
        updater = self.fixture.bootstrap(self.fixture.package("release-one", 1))
        key = self.fixture.paths.releases / "release-one/meta/release-public-key.pem"
        saved = key.read_bytes()
        key.unlink()
        target = self.fixture.root / "replacement-key.pem"
        write(target, saved, 0o600)
        key.symlink_to(target)
        self.assertEqual(updater.active_path().name, "release-one")
        key.unlink()
        os.link(target, key)
        self.assertEqual(updater.active_path().name, "release-one")

    def test_packager_rejects_plaintext_preset_before_signing(self) -> None:
        output = self.fixture.root / "cosmo-release-release-two.tar.gz"
        with self.assertRaisesRegex(
            RuntimeError,
            "plaintext or unknown preset model blocks upgrade",
        ):
            self.fixture.package("release-two", 2, plaintext_preset=True)
        self.assertFalse(output.exists())

    def test_legacy_preset_is_rejected(self) -> None:
        model = b"\x01\x00\x01\xec" + (2).to_bytes(4, "little") + b"approved-ciphertext"
        model_path = self.fixture.root / "legacy-release/resource/models/preset-one/model.nn"
        write(model_path, model, 0o444)
        with self.assertRaisesRegex(
            release.ReleaseError, "plaintext or unknown preset"
        ):
            release._scan_preset_models(self.fixture.root / "legacy-release")

    def test_packager_accepts_hardlinked_payload_file(self) -> None:
        payload = self.fixture.payload("hardlinked")
        os.link(payload / "lib/libbmrt.so", payload / "lib/libbmrt-copy.so")
        archive = self.fixture.root / "cosmo-release-hardlinked.tar.gz"
        read_fd, write_fd = os.pipe()
        os.write(write_fd, self.fixture.private_key.read_bytes())
        os.close(write_fd)
        saved_three = os.dup(3) if os.path.exists("/proc/self/fd/3") else None
        try:
            os.dup2(read_fd, 3)
            result = subprocess.run(
                (
                    "/usr/bin/python3",
                    "-I",
                    "-B",
                    str(PACKAGER),
                    "--payload",
                    str(payload),
                    "--output",
                    str(archive),
                    "--release-id",
                    "hardlinked",
                    "--generation",
                    "2",
                    "--release-public-key",
                    str(self.fixture.public_key),
                ),
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                pass_fds=(3,),
                check=False,
                env={"LC_ALL": "C", "PATH": "/usr/bin:/bin"},
            )
        finally:
            os.close(read_fd)
            if saved_three is None:
                os.close(3)
            else:
                os.dup2(saved_three, 3)
                os.close(saved_three)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertTrue(archive.exists())

    def test_packager_rejects_bootstrap_with_different_hidden_trust_key(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "bootstrap trust anchor"):
            self.fixture.package(
                "wrong-bootstrap-trust",
                2,
                wrong_bootstrap_trust=True,
            )
        self.assertFalse(
            (self.fixture.root / "cosmo-release-wrong-bootstrap-trust.tar.gz").exists()
        )

    def test_packager_accepts_bootstrap_dynamic_contract(self) -> None:
        archive = self.fixture.package("audited-bootstrap", 2)
        self.assertTrue(archive.is_file())

    def test_packager_isolated_startup_ignores_malicious_pythonpath(
        self,
    ) -> None:
        malicious = self.fixture.root / "malicious-python-startup"
        marker = self.fixture.root / "sitecustomize-executed"
        write(
            malicious / "sitecustomize.py",
            (
                "import os\n"
                f"open({str(marker)!r}, 'wb').write(b'executed')\n"
                "os.read(3, 1)\n"
            ).encode("ascii"),
            0o600,
        )
        payload = self.fixture.payload("isolated-startup")
        archive = self.fixture.package_existing(
            payload,
            "isolated-startup",
            2,
            extra_environment={"PYTHONPATH": str(malicious)},
        )
        self.assertTrue(archive.is_file())
        self.assertFalse(marker.exists())

        unsafe = subprocess.run(
            ("/usr/bin/python3", "-B", str(PACKAGER), "--help"),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            env={"LC_ALL": "C", "PATH": "/usr/bin:/bin"},
        )
        self.assertNotEqual(unsafe.returncode, 0)
        self.assertIn(b"must be launched", unsafe.stderr)

    def test_packager_missing_fd3_fails_without_descriptor_reuse(self) -> None:
        payload = self.fixture.payload("missing-fd3")
        archive = self.fixture.root / "cosmo-release-missing-fd3.tar.gz"
        command = (
            "/usr/bin/python3",
            "-I",
            "-B",
            str(PACKAGER),
            "--payload",
            str(payload),
            "--output",
            str(archive),
            "--release-id",
            "missing-fd3",
            "--generation",
            "2",
            "--release-public-key",
            str(self.fixture.public_key),
        )
        existing = self.fixture.root / "cosmo-release-existing-no-fd3.tar.gz"
        write(existing, b"existing", 0o600)
        existing_command = list(command)
        existing_command[existing_command.index("--output") + 1] = str(
            existing
        )
        public_input_failure = subprocess.run(
            existing_command,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            close_fds=True,
            check=False,
            env={"LC_ALL": "C", "PATH": "/usr/bin:/bin"},
        )
        self.assertEqual(public_input_failure.returncode, 1)
        self.assertEqual(public_input_failure.stdout, b"")
        self.assertEqual(
            public_input_failure.stderr,
            b"release packaging failed: release output already exists\n",
        )
        self.assertEqual(existing.read_bytes(), b"existing")

        result = subprocess.run(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            close_fds=True,
            check=False,
            env={"LC_ALL": "C", "PATH": "/usr/bin:/bin"},
        )
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, b"")
        self.assertEqual(
            result.stderr,
            (
                b"release packaging failed: release signing key must be "
                b"supplied on inherited fd 3\n"
            ),
        )
        self.assertFalse(archive.exists())

    def test_release_output_gate_is_no_replace_and_precedes_fd3(self) -> None:
        payload = self.fixture.payload("existing-release-output")
        archive = (
            self.fixture.root
            / "cosmo-release-existing-release-output.tar.gz"
        )
        write(archive, b"preexisting-output", 0o600)
        with self.assertRaises(RuntimeError) as error:
            self.fixture.package_existing(
                payload,
                "existing-release-output",
                2,
                signing_key_bytes=b"",
            )
        self.assertNotIn(
            "release signing key length rejected", str(error.exception)
        )
        self.assertEqual(archive.read_bytes(), b"preexisting-output")

        uncontrolled = self.fixture.root / "uncontrolled-output"
        uncontrolled.mkdir(mode=0o777)
        os.chmod(uncontrolled, 0o777)
        uncontrolled_target = packager._prepare_controlled_output(
            str(uncontrolled / "release.tar.gz")
        )
        os.close(uncontrolled_target.directory_fd)

        def assert_race_is_before_secret(entry: str) -> None:
            output_path = (
                self.fixture.root / f"race-{entry}-release.tar.gz"
            )
            target = packager._prepare_controlled_output(str(output_path))
            read_fd, write_fd = os.pipe()
            os.write(write_fd, b"fd3-remains-unread")
            os.close(write_fd)
            try:
                name = (
                    target.output_name
                    if entry == "output"
                    else target.temporary_name
                )
                descriptor = os.open(
                    name,
                    os.O_WRONLY
                    | os.O_CREAT
                    | os.O_EXCL
                    | os.O_CLOEXEC
                    | os.O_NOFOLLOW,
                    0o600,
                    dir_fd=target.directory_fd,
                )
                os.close(descriptor)
                try:
                    saved_three = os.dup(3)
                except OSError:
                    saved_three = None
                try:
                    os.dup2(read_fd, 3)
                    with self.assertRaises(RuntimeError):
                        packager._validate_output_before_secret(target)
                finally:
                    if saved_three is None:
                        os.close(3)
                    else:
                        os.dup2(saved_three, 3)
                        os.close(saved_three)
                self.assertEqual(os.read(read_fd, 64), b"fd3-remains-unread")
            finally:
                os.close(read_fd)
                with contextlib.suppress(FileNotFoundError):
                    os.unlink(name, dir_fd=target.directory_fd)
                os.close(target.directory_fd)

        for entry in ("output", "temporary"):
            with self.subTest(entry=entry):
                assert_race_is_before_secret(entry)

        collision_path = self.fixture.root / "rename-collision.tar.gz"
        collision_target = packager._prepare_controlled_output(
            str(collision_path)
        )
        original_rename = packager._rename_output_noreplace

        def create_collision_then_rename(
            directory_fd: int, old_name: str, new_name: str
        ) -> None:
            descriptor = os.open(
                new_name,
                os.O_WRONLY
                | os.O_CREAT
                | os.O_EXCL
                | os.O_CLOEXEC
                | os.O_NOFOLLOW,
                0o600,
                dir_fd=directory_fd,
            )
            try:
                os.write(descriptor, b"attacker-output")
            finally:
                os.close(descriptor)
            original_rename(directory_fd, old_name, new_name)

        try:
            with mock.patch.object(
                packager,
                "_rename_output_noreplace",
                side_effect=create_collision_then_rename,
            ):
                with self.assertRaisesRegex(
                    RuntimeError, "no-replace release publication failed"
                ):
                    packager._write_bundle(
                        collision_target,
                        b"{}",
                        b"s" * 64,
                        b"{}",
                        (),
                        {},
                    )
            self.assertEqual(collision_path.read_bytes(), b"attacker-output")
            self.assertFalse(
                (
                    collision_target.parent
                    / collision_target.temporary_name
                ).exists()
            )
        finally:
            with contextlib.suppress(FileNotFoundError):
                collision_path.unlink()
            os.close(collision_target.directory_fd)

        failed_sync_path = self.fixture.root / "failed-parent-sync.tar.gz"
        failed_sync_target = packager._prepare_controlled_output(
            str(failed_sync_path)
        )
        try:
            with mock.patch.object(
                packager.os,
                "fsync",
                side_effect=OSError("injected parent fsync failure"),
            ):
                with self.assertRaisesRegex(
                    OSError, "injected parent fsync failure"
                ):
                    packager._write_bundle(
                        failed_sync_target,
                        b"{}",
                        b"s" * 64,
                        b"{}",
                        (),
                        {},
                    )
            self.assertFalse(failed_sync_path.exists())
            self.assertFalse(
                (
                    failed_sync_target.parent
                    / failed_sync_target.temporary_name
                ).exists()
            )
        finally:
            os.close(failed_sync_target.directory_fd)

    def test_packager_uses_one_private_snapshot_after_source_replacement(self) -> None:
        payload = self.fixture.payload("snapshot-race")
        guard_path = payload / f"lib/{release.GUARD_REAL_FILENAME}"
        admitted_guard = guard_path.read_bytes()
        output = self.fixture.root / "cosmo-release-snapshot-race.tar.gz"
        real_snapshot = packager._snapshot_payload

        def snapshot_then_replace(source: Path, destination: Path) -> None:
            real_snapshot(source, destination)
            guard_path.write_bytes(b"attacker replacement after snapshot")

        read_fd, write_fd = os.pipe()
        os.write(write_fd, self.fixture.private_key.read_bytes())
        os.close(write_fd)
        try:
            saved_three = os.dup(3)
        except OSError:
            saved_three = None
        try:
            os.dup2(read_fd, 3)
            with mock.patch.object(
                packager, "_snapshot_payload", side_effect=snapshot_then_replace
            ):
                packager.build_bundle(
                    types.SimpleNamespace(
                        payload=str(payload),
                        output=str(output),
                        release_id="snapshot-race",
                        generation=2,
                        release_public_key=str(self.fixture.public_key),
                    )
                )
        finally:
            os.close(read_fd)
            if saved_three is None:
                with contextlib.suppress(OSError):
                    os.close(3)
            else:
                os.dup2(saved_three, 3)
                os.close(saved_three)
        with tarfile.open(output, "r:gz") as archive:
            bundled_guard = archive.extractfile(
                f"payload/lib/{release.GUARD_REAL_FILENAME}"
            )
            self.assertIsNotNone(bundled_guard)
            assert bundled_guard is not None
            self.assertEqual(bundled_guard.read(), admitted_guard)

    def test_packager_rejects_model_guard_test_marker_by_name_or_content(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "test-fixture marker"):
            self.fixture.package(
                "marked-model-guard",
                2,
                model_guard_test_marker=True,
            )
        payload = self.fixture.payload("renamed-marker")
        write(
            payload / "share/cosmo-model-guard/renamed-profile",
            b"COSMO_MODEL_GUARD_V2_TEST_FIXTURE_DO_NOT_DEPLOY\n",
        )
        archive = self.fixture.root / "cosmo-release-renamed-marker.tar.gz"
        read_fd, write_fd = os.pipe()
        os.write(write_fd, self.fixture.private_key.read_bytes())
        os.close(write_fd)
        try:
            saved_three = os.dup(3)
        except OSError:
            saved_three = None
        try:
            os.dup2(read_fd, 3)
            result = run(
                (
                    "/usr/bin/python3",
                    "-I",
                    "-B",
                    str(PACKAGER),
                    "--payload",
                    str(payload),
                    "--output",
                    str(archive),
                    "--release-id",
                    "renamed-marker",
                    "--generation",
                    "2",
                    "--release-public-key",
                    str(self.fixture.public_key),
                )
            )
        finally:
            os.close(read_fd)
            if saved_three is None:
                os.close(3)
            else:
                os.dup2(saved_three, 3)
                os.close(saved_three)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"marker content", result.stderr)
        self.assertFalse(archive.exists())

    def test_packager_rejects_all_zero_guard_trust_without_marker(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "product-pepper bundle format"):
            self.fixture.package(
                "zero-guard-trust",
                2,
                zero_model_guard_trust=True,
            )

    def test_packager_requires_all_fixed_guard_trust_sections(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "trust section count rejected"):
            self.fixture.package(
                "missing-guard-trust",
                2,
                omit_model_guard_trust_symbol=(
                    "cmg_commissioning_public_key_bundle_v1"
                ),
            )

    def test_packager_rejects_bootstrap_dynamic_contract_variants(self) -> None:
        cases = (
            (
                "missing-crypto",
                {"bootstrap_link_crypto": False},
                "NEEDED set",
            ),
            (
                "wrong-runpath",
                {"bootstrap_runpath": "$ORIGIN"},
                "RUNPATH/RPATH contract",
            ),
            (
                "old-rpath",
                {"bootstrap_old_dtags": True},
                "RUNPATH/RPATH contract",
            ),
            (
                "extra-needed",
                {"bootstrap_extra_needed": True},
                "NEEDED set",
            ),
        )
        for name, options, message in cases:
            with self.subTest(case=name):
                with self.assertRaisesRegex(RuntimeError, message):
                    self.fixture.package(name, 2, **options)

    def test_packager_requires_executable_model_provision_tool(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "missing required compatibility files"):
            self.fixture.package(
                "missing-model-provision",
                2,
                omit_model_provision=True,
            )
        with self.assertRaisesRegex(RuntimeError, "cosmo-model-provision must be executable"):
            self.fixture.package(
                "nonexec-model-provision",
                2,
                model_provision_mode=0o644,
            )

    def test_updater_layout_requires_model_provision_file(self) -> None:
        payload = self.fixture.payload("layout-model-provision")
        release._check_release_layout(payload)
        provision = payload / release.MODEL_PROVISION_PATH
        provision.unlink()
        with self.assertRaises(release.ReleaseError):
            release._check_release_layout(payload)
        write(provision, b"#!/bin/sh\nexit 1\n", 0o644)
        release._check_release_layout(payload)

    def test_packager_rejects_python_bytecode_cache(self) -> None:
        payload = self.fixture.payload("bytecode")
        write(payload / "scripts/__pycache__/release_updater.cpython-310.pyc", b"not-bytecode")
        archive = self.fixture.root / "cosmo-release-bytecode.tar.gz"
        read_fd, write_fd = os.pipe()
        os.write(write_fd, self.fixture.private_key.read_bytes())
        os.close(write_fd)
        try:
            saved_three = os.dup(3)
        except OSError:
            saved_three = None
        try:
            os.dup2(read_fd, 3)
            result = subprocess.run(
                (
                    "/usr/bin/python3",
                    "-I",
                    "-B",
                    str(PACKAGER),
                    "--payload",
                    str(payload),
                    "--output",
                    str(archive),
                    "--release-id",
                    "bytecode",
                    "--generation",
                    "2",
                    "--release-public-key",
                    str(self.fixture.public_key),
                ),
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                pass_fds=(3,),
                check=False,
                env={"LC_ALL": "C", "PATH": "/usr/bin:/bin"},
            )
        finally:
            os.close(read_fd)
            if saved_three is None:
                os.close(3)
            else:
                os.dup2(saved_three, 3)
                os.close(saved_three)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"bytecode", result.stderr.lower())
        self.assertFalse(archive.exists())

    def test_release_public_key_object_is_aarch64_and_has_frozen_symbols(self) -> None:
        output = self.fixture.root / "release-public-key.o"
        result = run(
            (
                "/usr/bin/python3",
                "-I",
                "-B",
                str(PUBLIC_KEY_OBJECT_GENERATOR),
                "--public-key",
                str(self.fixture.public_key),
                "--output",
                str(output),
            )
        )
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        header = run(("/usr/bin/aarch64-linux-gnu-readelf", "-hW", str(output)))
        symbols = run(("/usr/bin/aarch64-linux-gnu-nm", "-g", str(output)))
        self.assertEqual(header.returncode, 0, header.stderr.decode())
        self.assertIn(b"AArch64", header.stdout)
        self.assertEqual(symbols.returncode, 0, symbols.stderr.decode())
        for symbol in (
            b"cosmo_release_public_key_raw_v1",
            b"cosmo_release_public_key_id_v1",
            b"cosmo_release_public_key_pem_sha256_v1",
        ):
            self.assertIn(symbol, symbols.stdout)

        unsafe = run(
            (
                "/usr/bin/python3",
                "-B",
                str(PUBLIC_KEY_OBJECT_GENERATOR),
                "--help",
            )
        )
        self.assertNotEqual(unsafe.returncode, 0)
        self.assertIn(b"must be launched", unsafe.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
