# Conky — Windows Port (MinGW)

## Project Nature

This is a **Windows port of Conky** (system monitor) using **MinGW (TDM-GCC-64
g++ 10.3.0)**. The codebase is forked from upstream Linux Conky; Linux/X11
backends are guarded behind `#ifdef`s and are not compiled here. The Windows
display backend lives in `src/output/display-windows.{cc,hh}`.

## Build System

- **CMake** with **MinGW Makefiles** (`-G "MinGW Makefiles"`)
- Run from `build/` directory: `cmake -G "MinGW Makefiles" ..` then `mingw32-make.exe`
- No Ninja, no mise, no clang-format targets available on this platform
- 3rdparty dependencies: `3rdparty/intl-stub/` (gettext stubs), `3rdparty/Vc`, `3rdparty/spdlog`, `3rdparty/toluapp`
- NVML (NVIDIA Management Library) is enabled for GPU stats: `-DBUILD_NVIDIA_NVML=ON`

### Typical build cycle

```powershell
cd build
cmake -G "MinGW Makefiles" -DBUILD_NVIDIA_NVML=ON ..
mingw32-make.exe -j$(nproc)
```

If `conky.exe` is locked (permission denied), kill old processes first:
`taskkill /F /IM conky.exe 2>$null`

### Building individual changes

After editing `display-windows.cc`, rebuild just the relevant object and relink:
```powershell
mingw32-make.exe -j$(nproc) src/CMakeFiles/conky.dir/output/display-windows.cc.obj
mingw32-make.exe -j$(nproc) conky.exe
```

## Windows Display Architecture

The display backend (`display_output_windows`) renders Conky text onto the
desktop using a layered window with per-pixel alpha:

1. **Window type**: WS_POPUP (top-level), not WS_CHILD — avoids parent-relative
   coordinate issues with Progman/WorkerW on Win11
2. **Extended styles**: WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW
3. **Rendering**: 32-bit DIB section with BI_RGB (B,G,R,A byte order), rendered
   via `UpdateLayeredWindow` with `AC_SRC_ALPHA` for per-pixel alpha compositing
4. **Alpha fix**: GDI drawing (`FillRect`, `DrawText`) does NOT write alpha
   channel — a post-draw loop sets `pixels[i*4+3] = 255` to make GDI output visible
5. **Window placement**: Positioned on the primary monitor's work area, `top_right`
   alignment with configurable gap

### Monitor setup

- Primary: 2560×1440 at origin (0,0)
- Secondary: 1920×1200 at (-1920, 0)
- Virtual desktop: -1920 to 2560 horizontally

### Key files

| Path | Purpose |
|---|---|
| `src/output/display-windows.cc` | Core Windows display: window creation, DIB, UpdateLayeredWindow, text drawing |
| `src/output/display-windows.hh` | Header for `display_output_windows` class |
| `src/conky.cc` | Main loop, config parsing, text object evaluation |
| `src/core.cc` | Text object registration (`OBJ`, `OBJ_ARG` macros) |
| `src/common.cc` | Platform-independent helpers (uptime, memory, disk detection) |
| `src/data/os/windows/` | Windows-specific data collection (CPU, memory, disk I/O, network) |
| `src/data/os/windows/win32_nvml.cc` | NVML GPU temperature/fan/memory wrappers |
| `3rdparty/intl-stub/intl.h` | Stub `gettext` — all strings pass through untranslated |

## Configuration

Sample config: `C:\Users\Peto\claude_playground\conky_examples\Windows.conkyrc`

Key settings:
- `alignment = 'top_right'` — Conky positions relative to monitor work area
- `own_window = true` — enables the layered window
- `gap_x = 20, gap_y = 60` — offset from screen edge
- `minimum_width = 340` — forces minimum window width
- GPU vars use `${nvidia gputemp 0}` etc. (NVML backend)

## Testing

- No automated test suite for the Windows port
- Manual: build `conky.exe`, run from `conky_examples/` directory
- To verify window content: use PrintWindow-based capture (GDI CopyFromScreen
  cannot reliably capture layered windows on Win11)
- Check log output for `windows display:` prefixed log lines

## Debugging Visibility Issues

If the Conky window isn't visible on screen:

1. **Confirm window exists**: `(Get-Process conky).MainWindowHandle` or
   `[System.Diagnostics.Process]::GetProcessesByName("conky")`
2. **Confirm position**: Use the diagnostic overlay in `end_draw_text()` (blue
   fill + checkerboard pattern)
3. **Check Z-order**: Temporarily remove `WS_EX_TRANSPARENT` or test with
   `SetWindowPos(hwnd_, HWND_TOPMOST, ...)`
4. **Capture window content**: PrintWindow with `PW_RENDERFULLCONTENT` captures
   layered window backing store
5. **Verify pixel alpha**: After the alpha fix loop, all pixels should have
   alpha=255 — read back and dump a sample row if needed

### Critical: OWN_WINDOW Coordinate System

`OWN_WINDOW` is `#undef` in the Windows build (`config.h`). This causes
`conky.cc` to compute `text_start` as **screen-absolute** coordinates, not
window-relative. All drawing functions in `display-windows.cc` MUST subtract
`window_rect_.left/top` from incoming coordinates before GDI calls.

Key files and lines in `display-windows.cc`:
- `draw_string_at()` line 456-457: `cx = x - window_rect_.left; cy = y - window_rect_.top;`
- `draw_line()` line 477-478: subtract from each endpoint
- `draw_rect()` line 485-486: subtract from x,y
- `fill_rect()` line 499-500: subtract from x,y
- `begin_draw_text()` line 331: sync `GetWindowRect()` at start
- `move_win()` line 512: sync after SetWindowPos
- `end_draw_text()` line 430-431: use `window_rect_.left/top` for ULW position
- `resize_to_content()` line 604: sync after SetWindowPos

Removing these subtractions causes all text to be drawn off-screen (into the
DIB at pixel coordinates like 2180,60 when the DIB is only 351 pixels wide),
resulting in an empty/gray window with no visible content.

## Coding Style

- Follow the upstream Conky style for existing files (2-space indent, etc.)
- Windows-specific additions can use Windows API conventions (snake_case
  Win32 types, hungarian notation where it aids clarity)
- Keep Windows code behind existing `#ifdef BUILD_WIN32` guards (defined via
  ConkyPlatformChecks.cmake)
- Minimize `#ifdef` spaghetti: put platform-specific implementations in
  separate source files, not inline conditionals
