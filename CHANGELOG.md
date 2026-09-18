# Changelog

All notable changes to this Windows port are documented here. Format
follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/); this
project doesn't follow upstream conky's own versioning, so tags here are
specific to the Windows port.

Every entry links the GitHub issue that tracked it — see the Workflow
section in [AGENTS.md](AGENTS.md) for how work flows from issue to
`develop` to a tagged release.

## [Unreleased]

Changes land here as they're merged to `develop`, then move under a
version heading when that state gets tagged and merged to `main`.

### Fixed
- A spdlog write/rotation failure (e.g. from rapid config reloads,
  #34, hitting the log file while another handle briefly has it open)
  crashed the whole process instead of being handled by this project's
  own non-fatal error handler (`src/logging.cc`). Root cause:
  `3rdparty/spdlog/CMakeLists.txt` had `SPDLOG_NO_EXCEPTIONS` forced ON
  with no reason to (nothing else here builds with `-fno-exceptions`),
  which makes spdlog `printf`+`abort()` directly instead of throwing --
  completely bypassing the custom handler regardless of what it does.
  Reproduced reliably (a rapid-reload stress test killed the process
  within ~50-60 cycles every time) and confirmed fixed (220+ rapid
  saves across multiple runs, zero crashes, stable GDI/USER handle
  counts) after flipping the
  flag. [#42](https://github.com/naqoyqatsi83/conky-windows/issues/42)
- `${cpugraph}` intermittently drew a stray line spanning almost the
  full window height for one frame right after `conky.exe` started.
  `info.cpu_usage` was allocated with `malloc` (uninitialized); the
  first-ever CPU% sample has no prior baseline to diff against, so it's
  correctly skipped, but that left `${cpu}`/`${cpugraph}` reading raw
  heap garbage for that one frame. Fixed by allocating with `calloc`
  instead, plus a defensive clamp on the graph bar's drawn extent
  (matching the existing `grad_idx` clamp in the same function, from
  the issue #1 SIGSEGV investigation) so any future bad value degrades
  to a malformed-but-contained bar rather than a stray full-height
  line. Reproduced reliably (21 anomalies / 20 fresh launches) and
  confirmed fixed (0 / 20) via temporary diagnostic
  logging. [#33](https://github.com/naqoyqatsi83/conky-windows/issues/33)
- The installer's `CreateConfig()` unconditionally regenerated
  `{userdocs}\Conky\btop.conkyrc` from the install-time template on
  *every* reinstall, silently overwriting any customization (including
  anything tuned via the live-preview editor, #31). Now checks
  `FileExists` first and asks via a dialog on an interactive install
  (silent installs always keep the existing file -- no one to click a
  dialog). Also renamed the sample config `btop.conkyrc` -> `conkyrc`
  (generic default name, no pre-release installed base worth migrating
  for). [#41](https://github.com/naqoyqatsi83/conky-windows/issues/41)

### Added
- Live config-reload on Windows: editing and saving a running instance's
  config file now updates it in place, no restart needed -- parity with
  Linux's inotify-based auto-reload, which was entirely absent on this
  port (`HAVE_SYS_INOTIFY_H` is Linux-only). Since Windows has no
  inotify equivalent wired up here, `main_loop()` polls the config
  file's mtime once per update cycle instead (bounded latency, no
  extra thread) and calls the existing cross-platform `reload_config()`
  -- respects `disable_auto_reload` same as Linux. Verified live: edited
  a running instance's config, confirmed it picked up the change with
  the same PID (no process restart) via a debug log grep and a
  before/after screenshot; separately confirmed `disable_auto_reload =
  true` suppresses
  it. [#34](https://github.com/naqoyqatsi83/conky-windows/issues/34)
- `tools/conky_editor/`'s preview now uses #34's live-reload instead of
  killing and relaunching `conky.exe` on every edit:
  `PreviewProcess.sync()` only spawns a process for the first launch (or
  if the preview isn't running for any reason); an already-running
  preview just gets its temp file rewritten and reloads itself. Avoids a
  full OS process respawn (and the GPU-sensor scheduled-task retrigger
  that comes with every `conky.exe` launch) on every debounce tick.
  Verified: 5 rapid text edits kept the same PID throughout (no
  respawn), and an alignment-grid change correctly repositioned the
  window via live-reload with no respawn
  either. [#35](https://github.com/naqoyqatsi83/conky-windows/issues/35)
- `tools/conky_editor/`: the text pane / side panel split is now a
  draggable `QSplitter` instead of a fixed 3:1
  layout. [#37](https://github.com/naqoyqatsi83/conky-windows/issues/37)
- `tools/conky_editor/`: the Lint panel now wraps long findings instead of
  running off the visible width, and a new **View** menu (the editor's
  first menu bar) has a "Wrap Long Lines" toggle for the main text
  pane. [#38](https://github.com/naqoyqatsi83/conky-windows/issues/38)
- `tools/conky_editor/`: the main text pane now has line numbers
  (`line_number_edit.py`, the standard Qt "Code Editor" gutter pattern)
  -- one number per logical line, drawn once at its first visual row, so
  a long line wrapped into several rows via "Wrap Long Lines" still shows
  a single correct number instead of miscounting. Verified visually with
  wrap on: a 4-visual-row wrapped line shows its number
  once. [#38](https://github.com/naqoyqatsi83/conky-windows/issues/38)
- `tools/conky_editor/`: a "Show Line Numbers" View menu toggle (on by
  default), and the View menu's toggle states (wrap, line numbers) now
  persist across runs via `QSettings` in INI format --
  `%APPDATA%\ConkyWindows\ConkyEditor.ini`, a plain text file rather than
  the registry, easy to find/inspect/delete. Verified: defaults are
  correct on a fresh settings file (wrap off, line numbers on), toggling
  both and relaunching a fresh window instance loads the persisted
  values correctly, and hiding line numbers collapses the gutter to zero
  width as expected -- with no regression to preview/live-reload/lint
  wired up alongside
  it. [#38](https://github.com/naqoyqatsi83/conky-windows/issues/38)
- `tools/conky_editor/`: a live-preview GUI config editor (PySide6) --
  text pane + a real, live-updating `conky.exe` preview, plus a 3x3
  anchor grid and `gap_x`/`gap_y` fields mirroring conky's own
  positioning model. Milestones 1-2 (debounced preview, positioning
  grid) verified: no orphaned `conky.exe` processes across repeated
  edits/restarts, and all 9 grid positions confirmed via
  `GetWindowRect` to land the window at the expected screen
  corner/edge. [#31](https://github.com/naqoyqatsi83/conky-windows/issues/31)
- `xinerama_head` config key on Windows: selects which monitor the window
  is placed on (0-indexed, matching `${monitor}`'s own enumeration order),
  reusing upstream's X11 head-selection key name. Defaults to -1 (primary
  monitor, unchanged from prior behavior); out-of-range values fall back
  to primary with a logged warning. Verified on this machine's real
  2-monitor setup: `xinerama_head = 1` lands the window on the secondary
  monitor, `0`/unset/out-of-range all correctly resolve to
  primary. [#32](https://github.com/naqoyqatsi83/conky-windows/issues/32)
- `tools/conky_editor/`: a monitor picker (`monitors.py` + a combo box)
  wired to `xinerama_head`, driving the same preview loop as the alignment
  grid. Verified end-to-end on this machine's real 2-monitor setup:
  selecting the secondary monitor moves the live preview window there
  (`GetWindowRect` confirmed), and switching back to "Default" removes the
  `xinerama_head` key rather than leaving a redundant explicit
  `-1`. [#32](https://github.com/naqoyqatsi83/conky-windows/issues/32)
- `tools/conky_editor/`: milestone 3 -- minimal syntax highlighting
  (`highlighter.py`, `${...}` object references only, reusing
  `backport_conkyrc.py`'s brace-depth-aware `scan_objects()`) and a live
  "Lint" side panel running `backport_conkyrc.lint()` on the same debounce
  cycle as the preview, surfacing every REVIEW/UNSUPPORTED/UNKNOWN/CONFIG/
  EXEC finding as you type. Verified against the repo's own example theme
  (57 OK / 11 findings, matching the CLI tool's own output) and that
  introducing a bogus object gets flagged UNKNOWN within one debounce
  cycle, with no regression to the preview-restart
  path. [#31](https://github.com/naqoyqatsi83/conky-windows/issues/31)
- `tools/conky_editor/conky_editor.spec`: PyInstaller packaging for the
  editor (`python -m PyInstaller tools/conky_editor/conky_editor.spec`) --
  a standalone `ConkyEditor.exe`, no Python install required to run it.
  `preview.py`'s default `conky.exe` lookup is now frozen-build-aware
  (looks next to the packaged `.exe` instead of the dev-tree `build/src/`
  path), and `main.py` gained a `--conky-exe PATH` override plus a status-bar
  message instead of a crash when `conky.exe` isn't found. Verified: the
  packaged exe launches standalone (highlighting, lint panel, and position
  controls all render correctly) and its frozen `sys.executable` resolution
  was confirmed correct via a throwaway probe
  build. [#31](https://github.com/naqoyqatsi83/conky-windows/issues/31)
- `installer/setup.iss`: an opt-in "Install Conky Editor" task (unchecked
  by default, like the LibreHardwareMonitor task) that bundles
  `ConkyEditor.exe` next to `conky.exe` and adds a Start Menu shortcut.
  Built from `tools/conky_editor/dist/ConkyEditor.exe`
  (`skipifsourcedoesntexist`, so a normal conky-only build doesn't need it
  present). Verified the installer compiles and actually includes the exe
  (~61MB vs ~9MB without
  it). [#31](https://github.com/naqoyqatsi83/conky-windows/issues/31)

### Fixed
- Stale `mingw32-make.exe ... conky.exe` / `conky.dir` target names in
  `AGENTS.md`/`README.md`'s build instructions -- the actual CMake targets
  are `conky` and `conky_core.dir`. Also documented that `cmake`/
  `mingw32-make` must run from a shell with `sh` on PATH (Git Bash, not
  plain PowerShell): `colour-names.hh` is generated via a configure-time
  `sh`+`gperf` pipeline that fails silently without it, breaking the build
  with a confusing, seemingly-unrelated `content/colours.cc: 'rgb' does
  not name a type` error.

## [1.24.3-wp.4] - 2026-09-16

### Added
- `${apcupsd_*}` family on Windows (~13 objects) -- its NIS protocol is
  plain BSD sockets, needing only header/close()/WSAStartup fixes. [#25](https://github.com/naqoyqatsi83/conky-windows/issues/25)
- `${image}` on Windows, via a from-scratch GDI+ backend (Imlib2 itself
  is X11-adjacent and not a good porting target). Verified visually
  with PrintWindow against a generated test PNG. [#24](https://github.com/naqoyqatsi83/conky-windows/issues/24)
- `${user_names}`/`${user_times}`/`${user_time}`/`${user_terms}`/
  `${user_number}` on Windows, via the Terminal Services API
  (`WTSEnumerateSessions`). [#23](https://github.com/naqoyqatsi83/conky-windows/issues/23)
- `${rss}` on Windows, via a newly-vendored libxml2 build (`3rdparty/libxml2`,
  actively-maintained MSYS2 package) alongside its curl dependency. [#30](https://github.com/naqoyqatsi83/conky-windows/issues/30)
- `${mixer}`/`${mixerbar}`/`${mixerl}`/`${mixerr}`/`${if_mixer_mute}` on
  Windows, via Core Audio (`IAudioEndpointVolume`). No discrete named
  mixer channels like OSS, so a channel-name argument is ignored in
  favor of the default playback device's master volume/mute state. [#21](https://github.com/naqoyqatsi83/conky-windows/issues/21)
- `${wireless_*}` family on Windows (10 objects), via the Native Wifi API
  (`wlanapi.h`). Verified the adapter-detection and not-connected paths
  against real Wi-Fi hardware; the actively-connected data path wasn't
  testable end-to-end on the dev machine (no live Wi-Fi network
  configured there). [#20](https://github.com/naqoyqatsi83/conky-windows/issues/20)
- `${mpd_*}` family on Windows (~17 objects) -- fixed a Unix-domain-socket
  code path that unconditionally referenced `struct sockaddr_un` (doesn't
  exist on Windows), then a `SOCK_CLOEXEC` fallback macro that resolved
  to an unrelated nonzero value and produced an invalid socket type
  bitmask at runtime. [#19](https://github.com/naqoyqatsi83/conky-windows/issues/19)
- `${curl}`, `${github_notifications}`, and `${stock}` on Windows, via a
  newly-vendored curl build (`3rdparty/curl`). `${stock}`'s own hardcoded
  API endpoint turned out to be dead since ~2017, independent of this
  port; `${rss}` needs libxml2 too and is tracked separately in
  [#30](https://github.com/naqoyqatsi83/conky-windows/issues/30). [#18](https://github.com/naqoyqatsi83/conky-windows/issues/18)
- `${tcp_portmon}` on Windows, via a new `GetExtendedTcpTable()`-based
  connection gatherer (IPv4 and IPv6) reusing the existing portable
  connection-tracking core in `libtcp-portmon.cc`. [#13](https://github.com/naqoyqatsi83/conky-windows/issues/13)
- `${v6addrs}` on Windows -- was gated off at the CMake level
  (`BUILD_IPV6` restricted to Linux). Also fixed a dormant heap-corrupting
  buffer overflow in the adapter address memcpy, exposed for the first
  time by enabling IPv6 support. [#26](https://github.com/naqoyqatsi83/conky-windows/issues/26)
- Real Cairo support for `lua_draw_hook_pre`/`lua_draw_hook_post`, via a
  vendored Cairo build and a persistent off-screen surface (this port's
  per-frame DIB doesn't live long enough for hooks to draw into
  directly). Includes a compat shim so themes calling
  `cairo_xlib_surface_create()` directly work unmodified. [#10](https://github.com/naqoyqatsi83/conky-windows/issues/10)
- A systematic coverage audit of all 467 objects conky can register,
  cross-checked against what's actually wired up for Windows -- the
  source for most of the fixes in this release.

### Changed
- `tools/backport_conkyrc.py` now flags `${...}` objects that aren't real
  conky objects at all (typos, or long-deprecated upstream names like
  `${pre_exec}`) as a distinct `UNKNOWN` category instead of silently
  passing them as OK. [#8](https://github.com/naqoyqatsi83/conky-windows/issues/8)

### Fixed
- `conky.exe` could crash entirely once its log file hit the rotation
  threshold -- spdlog's default error handler aborted the process on a
  Windows-specific file-rename failure during rotation. [#27](https://github.com/naqoyqatsi83/conky-windows/issues/27)
- `${top}`/`${top_mem}` always printed blank -- the Windows process-list
  implementation wrote its results to the wrong internal list. [#17](https://github.com/naqoyqatsi83/conky-windows/issues/17)
- `pid_exe`/`pid_priority`/`pid_state`/`pid_threads`/`pid_vmsize`/
  `pid_vmrss`/`pid_vmpeak` were silently non-functional (read `/proc`,
  no Windows branch). [#16](https://github.com/naqoyqatsi83/conky-windows/issues/16)
- `${monitor}`/`${monitor_number}`, key/mouse state objects, and
  `${addrs}` were stubbed or unregistered on Windows. [#15](https://github.com/naqoyqatsi83/conky-windows/issues/15)
- `${if_mounted}`, `${if_running}`, and the default-gateway family
  (`gw_iface`/`gw_ip`/`if_gw`) were unregistered or silently broken. [#14](https://github.com/naqoyqatsi83/conky-windows/issues/14)
- Use-after-free when a Lua draw-hook theme correctly calls
  `cairo_surface_destroy()` on its drawing surface. [#28](https://github.com/naqoyqatsi83/conky-windows/issues/28)
- `${if_up}` was completely unregistered on Windows, and its absence
  cascaded into corrupting template parsing for everything after it. [#11](https://github.com/naqoyqatsi83/conky-windows/issues/11)
- `${threads}` had no Windows implementation. [#12](https://github.com/naqoyqatsi83/conky-windows/issues/12)
- Non-ASCII bytes (e.g. the degree sign) were mangled in debug logs due
  to a signed/unsigned `char` comparison -- not a real rendering bug,
  the actual GDI output was always correct. [#9](https://github.com/naqoyqatsi83/conky-windows/issues/9)
- CI: `patch.exe` missing on the MSYS2 runner once `BUILD_LUA_CAIRO` was
  forced on by default. [#29](https://github.com/naqoyqatsi83/conky-windows/issues/29)

## Earlier work

Everything before this changelog existed is in the git history and the
closed issues: [#1](https://github.com/naqoyqatsi83/conky-windows/issues/1) (SIGSEGV recovery hardening), [#2](https://github.com/naqoyqatsi83/conky-windows/issues/2) (test infrastructure), [#4](https://github.com/naqoyqatsi83/conky-windows/issues/4) (NVML soft dependency),
[#5](https://github.com/naqoyqatsi83/conky-windows/issues/5)/[#6](https://github.com/naqoyqatsi83/conky-windows/issues/6)/[#7](https://github.com/naqoyqatsi83/conky-windows/issues/7) (backport tool), the original GPU sensor pipeline fix, and the
installer/scheduled-task work.
