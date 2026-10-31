"""Compile and run the actual hardware-independent C++ firmware on the PC."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class FirmwareTests(unittest.TestCase):
    def test_native_firmware(self):
        compiler = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")
        if compiler is None:
            platformio_home = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
            bundled = platformio_home / "packages/toolchain-gccmingw32/bin/g++.exe"
            if bundled.exists():
                compiler = str(bundled)
        self.assertIsNotNone(
            compiler,
            "Install a C++ compiler or run: python -m platformio pkg install "
            "--global --tool platformio/toolchain-gccmingw32",
        )
        arduino_json = ROOT / ".pio/libdeps/atom-lite/ArduinoJson/src"
        self.assertTrue(arduino_json.exists(), "Run python -m platformio run first")
        with tempfile.TemporaryDirectory(prefix="turntable-tests-") as folder:
            executable = Path(folder) / ("firmware-tests.exe" if os.name == "nt" else "firmware-tests")
            command = [compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror",
                       "-Isrc", f"-I{arduino_json}", "tests/firmware_tests.cpp",
                       "src/controller.cpp", "src/ir_signal.cpp",
                       "src/signal_store.cpp", "src/usb_protocol.cpp", "-o", str(executable)]
            # MinGW's runtime DLLs are adjacent to its compiler, not on PATH.
            environment = dict(os.environ)
            environment["PATH"] = str(Path(compiler).resolve().parent) + os.pathsep + environment.get("PATH", "")
            result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, env=environment)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True, env=environment)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout.strip())
