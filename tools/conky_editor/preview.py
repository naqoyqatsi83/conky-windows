"""Manages a live conky.exe subprocess pointed at a config file.

The live preview is the real conky.exe renderer, not a reimplementation --
see issue #31. There's no usable cross-process reload signal on this Windows
port (SIGUSR1 is registered in src/conky.cc but POSIX signals can't be
delivered between unrelated Windows processes), so restart() kills and
relaunches the process rather than reloading in place.
"""

from __future__ import annotations

import subprocess
from pathlib import Path

DEFAULT_CONKY_EXE = Path(__file__).resolve().parents[2] / "build" / "src" / "conky.exe"


class PreviewProcess:
    """Owns a single managed conky.exe subprocess."""

    def __init__(self, conky_exe: Path = DEFAULT_CONKY_EXE) -> None:
        self.conky_exe = Path(conky_exe)
        self._proc: subprocess.Popen | None = None

    @property
    def running(self) -> bool:
        return self._proc is not None and self._proc.poll() is None

    def start(self, config_path: Path) -> None:
        if self.running:
            self.stop()
        config_path = Path(config_path)
        self._proc = subprocess.Popen(
            [str(self.conky_exe), "-c", str(config_path)],
            cwd=str(config_path.parent),
            creationflags=subprocess.CREATE_NEW_PROCESS_GROUP,
        )

    def stop(self) -> None:
        if self._proc is None:
            return
        if self._proc.poll() is None:
            self._proc.terminate()
            try:
                self._proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self._proc.kill()
                self._proc.wait(timeout=2)
        self._proc = None

    def restart(self, config_path: Path) -> None:
        self.stop()
        self.start(config_path)
