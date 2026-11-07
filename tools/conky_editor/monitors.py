"""Enumerates monitors in the same order conky's own resolve_target_monitor()
does (src/output/display-windows.cc), so an index picked here means the same
thing as the xinerama_head value written into the config.
"""

from __future__ import annotations

import ctypes
import ctypes.wintypes as wt
from dataclasses import dataclass

user32 = ctypes.windll.user32
user32.EnumDisplayMonitors.argtypes = [
    wt.HDC,
    ctypes.c_void_p,
    ctypes.WINFUNCTYPE(wt.BOOL, wt.HMONITOR, wt.HDC, ctypes.POINTER(wt.RECT), wt.LPARAM),
    wt.LPARAM,
]
user32.EnumDisplayMonitors.restype = wt.BOOL
user32.GetMonitorInfoW.argtypes = [wt.HMONITOR, ctypes.c_void_p]
user32.GetMonitorInfoW.restype = wt.BOOL

MONITORINFOF_PRIMARY = 0x1


class _MONITORINFOEX(ctypes.Structure):
    _fields_ = [
        ("cbSize", wt.DWORD),
        ("rcMonitor", wt.RECT),
        ("rcWork", wt.RECT),
        ("dwFlags", wt.DWORD),
        ("szDevice", wt.WCHAR * 32),
    ]


@dataclass
class Monitor:
    index: int
    name: str
    left: int
    top: int
    right: int
    bottom: int
    is_primary: bool

    @property
    def label(self) -> str:
        w, h = self.right - self.left, self.bottom - self.top
        tag = " (primary)" if self.is_primary else ""
        return f"{self.index}: {self.name} {w}x{h} @ ({self.left},{self.top}){tag}"


def list_monitors() -> list[Monitor]:
    monitors: list[Monitor] = []

    def enum_proc(hmon, hdc, lprect, lparam):
        mi = _MONITORINFOEX()
        mi.cbSize = ctypes.sizeof(_MONITORINFOEX)
        if user32.GetMonitorInfoW(hmon, ctypes.byref(mi)):
            monitors.append(
                Monitor(
                    index=len(monitors),
                    name=mi.szDevice,
                    left=mi.rcMonitor.left,
                    top=mi.rcMonitor.top,
                    right=mi.rcMonitor.right,
                    bottom=mi.rcMonitor.bottom,
                    is_primary=bool(mi.dwFlags & MONITORINFOF_PRIMARY),
                )
            )
        return True

    proc_type = ctypes.WINFUNCTYPE(
        wt.BOOL, wt.HMONITOR, wt.HDC, ctypes.POINTER(wt.RECT), wt.LPARAM
    )
    user32.EnumDisplayMonitors(None, None, proc_type(enum_proc), 0)
    return monitors
