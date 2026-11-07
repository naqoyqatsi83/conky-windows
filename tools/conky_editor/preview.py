"""Manages a live conky.exe subprocess pointed at a config file.

The live preview is the real conky.exe renderer, not a reimplementation --
see issue #31. conky.exe now watches its own config file's mtime and
reloads itself in-process (issue #34), so sync() just (re)writes the
managed temp file and lets the already-running process notice on its own
instead of killing and relaunching a whole new OS process on every edit --
that used to be the only option since there's no cross-process reload
signal on Windows (SIGUSR1 is registered in src/conky.cc but POSIX
signals can't be delivered between unrelated Windows processes). A full
respawn (start()/stop()) is still used for the very first launch, or if
the preview isn't running for any reason (e.g. the user closed it).
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path


def _default_conky_exe() -> Path:
    if getattr(sys, "frozen", False):
        # Packaged build (PyInstaller): __file__ points into the extracted
        # bundle, not anywhere near a real conky.exe. Expect conky.exe to
        # ship alongside the packaged editor .exe instead.
        return Path(sys.executable).resolve().parent / "conky.exe"
    return Path(__file__).resolve().parents[2] / "build" / "src" / "conky.exe"


DEFAULT_CONKY_EXE = _default_conky_exe()


class PreviewProcess:
    """Owns a single managed conky.exe subprocess."""

    def __init__(self, conky_exe: Path = DEFAULT_CONKY_EXE) -> None:
        self.conky_exe = Path(conky_exe)
        self._proc: subprocess.Popen | None = None
        self._config_path: Path | None = None

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
        self._config_path = config_path

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
        self._config_path = None

    def restart(self, config_path: Path) -> None:
        self.stop()
        self.start(config_path)

    def sync(self, config_path: Path) -> None:
        """(Re)spawn only if not already running this exact file.

        Call this *after* writing config_path's new content -- if the
        preview is already running against this same file, does nothing
        further: the running process's own config-file watcher (issue
        #34) notices the change and reloads itself. Only spawns/respawns
        if the preview isn't running yet, or is pointed at a different
        file.
        """
        config_path = Path(config_path)
        if self.running and self._config_path == config_path:
            return
        self.restart(config_path)
