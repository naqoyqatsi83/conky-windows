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
real, live-updating `conky.exe` preview, plus a 3x3 anchor grid, gap_x/gap_y
fields, and a monitor picker that together mirror conky's own positioning
model. See issue #31 for scope/rationale, #32 for the xinerama_head/
multi-monitor support the picker drives.

```powershell
python tools\conky_editor\main.py [path\to\theme.conkyrc]
```

The preview is driven by the actual `conky.exe` (`build\src\conky.exe` by
default), not a second rendering implementation: every edit is debounced
(~400ms) and written to a managed temp file. `conky.exe` watches its own
config file's mtime and reloads itself in-process (issue #34), so
`PreviewProcess.sync()` only spawns a fresh process for the very first
launch (or if the preview isn't running for any reason) -- an already-running
preview just gets its temp file rewritten and reloads itself, no process
kill+relaunch needed. See `preview.py`'s docstring for how this worked
before #34 landed (no cross-process reload signal existed on Windows).

`config_io.py` does targeted regex substitution of `alignment`/`gap_x`/
`gap_y`/`xinerama_head` in `conky.config = { ... }` -- like
`backport_conkyrc.py`, not a full Lua parser. `xinerama_head` is the one
exception to "only rewrites keys that already exist": most themes won't
have it, so the monitor picker inserts it when a non-default monitor is
picked and removes it again on "Default (primary)" rather than writing a
redundant `-1`. `monitors.py` enumerates monitors via `EnumDisplayMonitors`
in the same order `xinerama_head` indexes into
(`src/output/display-windows.cc`'s `resolve_target_monitor()`).

`line_number_edit.py`'s `LineNumberTextEdit` (the standard Qt "Code
Editor" gutter pattern) gives the main text pane line numbers -- one per
logical line, drawn once at that line's first visual row, so a long
`${exec ...}` line wrapped (View > Wrap Long Lines) into several rows
still shows a single, correct number instead of miscounting.

`highlighter.py` is a minimal `QSyntaxHighlighter`: `${...}` object
references are highlighted distinctly, reusing `backport_conkyrc.py`'s own
`scan_objects()` tokenizer so it's brace-depth aware (correctly skips over
`${...}`-looking text inside `${exec ...}` shell bodies, e.g. bash's
`${HOME}`) instead of a naive regex. Deliberately narrow scope per issue
#31's own plan -- no full Lua highlighting.

The "Lint" panel runs `backport_conkyrc.lint()` on the full editor text on
the same debounce cycle as the preview restart, listing every non-OK
finding (REVIEW / UNSUPPORTED / UNKNOWN / CONFIG / EXEC) with its line,
token, and message -- the same output the CLI tool would give you, just
live as you type instead of a one-shot report.

Requires PySide6 (`pip install PySide6`).

### Packaging as a standalone .exe

For people who shouldn't need a Python install to run the editor:

```powershell
pip install pyinstaller
python -m PyInstaller tools\conky_editor\conky_editor.spec
```

Produces `tools\conky_editor\dist\ConkyEditor.exe` -- a standalone,
double-clickable GUI app (no console window, no Python required). It does
*not* bundle `conky.exe` itself (that's a separate native binary this app
launches as a subprocess, not a Python dependency PyInstaller can embed):
copy `build\src\conky.exe` into the same folder as `ConkyEditor.exe` before
distributing it (the packaged build auto-detects a `conky.exe` next to its
own `.exe`), or point it elsewhere with `ConkyEditor.exe --conky-exe
PATH`. `dist/` and the intermediate `pyi_build/` are gitignored -- rebuild
locally rather than committing the ~45MB binary.
