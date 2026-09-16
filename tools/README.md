# Windows-port tools

## backport_conkyrc.py

Lints a Linux conky theme (Lua-format `.conkyrc`) for this Windows port and
flags what needs attention before it'll work here. See the script's own
docstring for full details, and issue #3 for scope/rationale.

```powershell
python tools\backport_conkyrc.py path\to\linux.conkyrc
python tools\backport_conkyrc.py path\to\linux.conkyrc -o windows.conkyrc -r report.md
```

It classifies every `${...}` object in `conky.text` as OK / needs review
(works, but has a Windows-specific gotcha) / unsupported (no Windows
equivalent) / exec (shell body, matched against a small POSIX->PowerShell
idiom table). It only ever rewrites `conky.text` in place for exec bodies it
can replace with high confidence — everything else is left untouched and
reported, since `conky.text` is a Lua long-bracket string with no comment
syntax to safely annotate inline.

This is a lint/starting-point tool, not a full transpiler. Extend the lookup
tables at the top of the script (`UNSUPPORTED_PREFIXES`, `UNSUPPORTED_EXACT`,
`REVIEW_OBJECTS`, `EXEC_IDIOMS`) as real themes surface cases it doesn't
handle yet.

No dependencies beyond the Python 3 standard library.

## conky_editor/

Live-preview GUI config editor: a text pane for the raw conkyrc next to a
real, live-updating `conky.exe` preview, plus a 3x3 anchor grid + gap_x/gap_y
fields that mirror conky's own positioning model. See issue #31 for
scope/rationale.

```powershell
python tools\conky_editor\main.py [path\to\theme.conkyrc]
```

The preview is driven by the actual `conky.exe` (`build\src\conky.exe` by
default), not a second rendering implementation: every edit is debounced
(~400ms), written to a managed temp file, and the previous preview process is
killed and a fresh one launched against it. There's no usable cross-process
config-reload signal on this Windows port to reload in place instead (see
`preview.py`'s docstring).

`config_io.py` does targeted regex substitution of `alignment`/`gap_x`/
`gap_y` in `conky.config = { ... }` -- like `backport_conkyrc.py`, not a full
Lua parser, and it only rewrites keys that already exist in the file.

Requires PySide6 (`pip install PySide6`).
