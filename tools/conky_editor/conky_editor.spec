# -*- mode: python ; coding: utf-8 -*-
"""PyInstaller spec for the live-preview conky config editor -- issue #31.

Build (from anywhere; SPECPATH below is this file's own directory):
    python -m PyInstaller tools/conky_editor/conky_editor.spec

Output: tools/conky_editor/dist/ConkyEditor.exe -- a standalone GUI app,
no Python install required to run it.

conky.exe itself is NOT bundled inside it -- it's a separate native binary
this app launches as a subprocess (preview.py), not a Python dependency
PyInstaller embeds. Ship the built ConkyEditor.exe in the same folder as
conky.exe (preview.py's frozen-build default looks there), or pass
--conky-exe PATH.
"""

import os

here = os.path.dirname(os.path.abspath(SPEC))
tools_dir = os.path.dirname(here)  # parent of tools/conky_editor/ -- for backport_conkyrc.py

a = Analysis(
    [os.path.join(here, "main.py")],
    pathex=[here, tools_dir],
    binaries=[],
    datas=[],
    hiddenimports=["backport_conkyrc"],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name="ConkyEditor",
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=False,
    console=False,
    disable_windowed_traceback=False,
)
