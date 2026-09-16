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

### Added
- Real Cairo support for `lua_draw_hook_pre`/`lua_draw_hook_post`, via a
  vendored Cairo build and a persistent off-screen surface (this port's
  per-frame DIB doesn't live long enough for hooks to draw into
  directly). Includes a compat shim so themes calling
  `cairo_xlib_surface_create()` directly work unmodified. [#10](https://github.com/naqoyqatsi83/conky-windows/issues/10)
- A systematic coverage audit of all 467 objects conky can register,
  cross-checked against what's actually wired up for Windows -- the
  source for most of the fixes above and the remaining backlog (see
  open issues [#13](https://github.com/naqoyqatsi83/conky-windows/issues/13), [#18](https://github.com/naqoyqatsi83/conky-windows/issues/18)-[#26](https://github.com/naqoyqatsi83/conky-windows/issues/26)).

## Earlier work

Everything before this changelog existed is in the git history and the
closed issues: [#1](https://github.com/naqoyqatsi83/conky-windows/issues/1) (SIGSEGV recovery hardening), [#2](https://github.com/naqoyqatsi83/conky-windows/issues/2) (test infrastructure), [#4](https://github.com/naqoyqatsi83/conky-windows/issues/4) (NVML soft dependency),
[#5](https://github.com/naqoyqatsi83/conky-windows/issues/5)/[#6](https://github.com/naqoyqatsi83/conky-windows/issues/6)/[#7](https://github.com/naqoyqatsi83/conky-windows/issues/7) (backport tool), the original GPU sensor pipeline fix, and the
installer/scheduled-task work.
