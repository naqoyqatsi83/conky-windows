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

### Added
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
