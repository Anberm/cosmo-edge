#!/usr/bin/python3
"""Container-only integration tests for the signed release health shell gate."""

from __future__ import annotations

import os
import shutil
import socket
import subprocess
import sys
import time
import unittest
import uuid
from pathlib import Path


REPOSITORY = Path(__file__).resolve().parents[1]
HEALTH_SCRIPT = REPOSITORY / "scripts/release_health_check.sh"
RELEASES = Path("/appfs/cosmo_wander/cwai_data/.releases")
GUARD_SONAME = "libcosmo_model_guard.so.2"

ENGINE_PROGRAM = r"""
import ctypes
import socket
import sys
import time

ctypes.CDLL(sys.argv[1])
libc = ctypes.CDLL(None)
if libc.prctl(15, b"cosmo-engine", 0, 0, 0) != 0:
    raise SystemExit(10)

def serve(duration):
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("127.0.0.1", 8000))
    listener.listen(8)
    listener.settimeout(0.1)
    deadline = None if duration is None else time.monotonic() + duration
    try:
        while deadline is None or time.monotonic() < deadline:
            try:
                connection, _ = listener.accept()
            except socket.timeout:
                continue
            with connection:
                connection.settimeout(0.5)
                try:
                    connection.recv(1024)
                    connection.sendall(
                        b"HTTP/1.0 204 No Content\r\nContent-Length: 0\r\n\r\n"
                    )
                except OSError:
                    pass
    finally:
        listener.close()

mode = sys.argv[2]
if mode == "steady":
    serve(None)
elif mode == "transient":
    serve(0.7)
elif mode == "flap":
    serve(1.4)
    time.sleep(1.2)
    serve(None)
else:
    raise SystemExit(11)
"""


class ReleaseHealthCheckTest(unittest.TestCase):
    def setUp(self) -> None:
        if os.geteuid() != 0:
            self.skipTest("health integration test requires an isolated root container")
        if shutil.which("pidof") is None:
            self.skipTest("target-compatible pidof utility is unavailable")
        self.release = RELEASES / f"health-test-{os.getpid()}-{uuid.uuid4().hex[:8]}"
        (self.release / "bin").mkdir(parents=True, mode=0o755)
        (self.release / "lib").mkdir(mode=0o755)
        os.symlink(
            str(Path(sys.executable).resolve()),
            self.release / "bin/cosmo-engine",
        )
        self.guard_target = self._loaded_shared_library()
        os.symlink(
            str(self.guard_target),
            self.release / f"lib/{GUARD_SONAME}",
        )
        self.engine: subprocess.Popen[bytes] | None = None

    def tearDown(self) -> None:
        if self.engine is not None and self.engine.poll() is None:
            self.engine.terminate()
            try:
                self.engine.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.engine.kill()
                self.engine.wait(timeout=3)
        if self.engine is not None and self.engine.stderr is not None:
            self.engine.stderr.close()
        if hasattr(self, "release") and self.release.is_dir():
            shutil.rmtree(self.release)

    @staticmethod
    def _loaded_shared_library() -> Path:
        for line in Path("/proc/self/maps").read_text(encoding="ascii").splitlines():
            fields = line.split()
            if not fields:
                continue
            candidate = Path(fields[-1])
            if (
                candidate.is_absolute()
                and ".so" in candidate.name
                and candidate.is_file()
            ):
                return candidate.resolve()
        raise RuntimeError("cannot locate a mapped shared library for the guard probe")

    def _start_engine(self, mode: str) -> None:
        engine = self.release / "bin/cosmo-engine"
        self.engine = subprocess.Popen(
            (
                str(engine),
                "-I",
                "-B",
                "-c",
                ENGINE_PROGRAM,
                str(self.guard_target),
                mode,
            ),
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            env={"LC_ALL": "C", "PATH": "/usr/bin:/bin"},
        )
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            if self.engine.poll() is not None:
                stderr = self.engine.stderr.read().decode() if self.engine.stderr else ""
                self.fail(f"synthetic engine exited before listening: {stderr}")
            try:
                with socket.create_connection(("127.0.0.1", 8000), timeout=0.1):
                    return
            except OSError:
                time.sleep(0.02)
        self.fail("synthetic engine did not open the health port")

    def _run_health(self) -> tuple[subprocess.CompletedProcess[bytes], float]:
        assert self.engine is not None
        started = time.monotonic()
        result = subprocess.run(
            (str(HEALTH_SCRIPT), str(self.engine.pid), str(self.release)),
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=15,
            env={"LC_ALL": "C", "PATH": "/usr/bin:/bin"},
        )
        return result, time.monotonic() - started

    def test_requires_three_consecutive_healthy_samples(self) -> None:
        self._start_engine("steady")
        result, elapsed = self._run_health()
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertGreaterEqual(elapsed, 1.8)

    def test_rejects_listener_that_disappears_before_stability_window(self) -> None:
        self._start_engine("transient")
        result, _ = self._run_health()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"exited before health acceptance", result.stderr)

    def test_listener_flap_resets_consecutive_sample_count(self) -> None:
        self._start_engine("flap")
        result, elapsed = self._run_health()
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertGreaterEqual(elapsed, 4.0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
