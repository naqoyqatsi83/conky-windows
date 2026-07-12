# Conky Windows Port — Status

## Toolchain

| Component | Version / Detail |
|---|---|
| Compiler | TDM-GCC-64 g++ 10.3.0 (MinGW) |
| Build system | CMake 3.17+, MinGW Makefiles |
| CMake flags | `-G "MinGW Makefiles" -DBUILD_NVIDIA_NVML=ON` |
| Binary | `build/conky.exe` |
| Config | `C:\Users\Peto\claude_playground\conky_examples\Windows.conkyrc` |

## What Works

- ✅ Window creation and positioning (layered WS_POPUP with per-pixel alpha)
- ✅ Text rendering via GDI (`DrawTextA`, `FillRect`, `DrawLine`)
- ✅ Desktop integration — top-level window at configured position
- ✅ NVML GPU temperature, fan speed, memory, frequency
- ✅ WMI-based CPU temperature
- ✅ PDH-based disk I/O (read/write bytes)
- ✅ Per-core CPU usage tracking
- ✅ Network stats (down/up speed, total, IP address)
- ✅ Memory and swap display
- ✅ Filesystem usage bars with correct bar drawing
- ✅ Text content rendering with proper screen-absolute→client coordinate conversion
- ✅ Resize to content height (window shrinks to match text_size)
- ✅ Top-right alignment with proper gap_x/gap_y positioning

## What's Not Tested / Not Working

- ❌ `acpitemp` variable — no ACPI on Windows; WMI backend returns -1°C on Ryzen 9800X3D
- ❌ Lua scripting — may work but not tested
- ❌ Mouse events (`${goto}`, clickable areas) — `WS_EX_TRANSPARENT` prevents clicks
- ❌ Automated tests — Catch2 suites not set up for MinGW
- ❌ Build configs other than MinGW (MSVC, clang-cl not tested)
- ❌ Multi-monitor alignment logic for secondary monitor

## Recently Resolved Issues

- ✅ **Graphs** (`${cpugraph}`, `${downspeedgraph}`, `${upspeedgraph}`) — now rendering via
  `draw_line()`. After the coordinate subtraction fix in `display-windows.cc`, conky's
  core `draw_graph_bars()` renders 1px-wide vertical bars automatically. No additional
  graph drawing code was needed.
- ✅ **Window content visibility** — coordinate subtraction fix in all drawing functions
  resolved the empty/gray window issue. Window renders correctly with all text,
  bars, and graphs.

## Known Issues

- GDI `CopyFromScreen` cannot reliably capture layered window content;
  `PrintWindow` with `PW_RENDERFULLCONTENT` works
- GDI drawing functions (`FillRect`, `DrawText`) don't write alpha channel;
  a post-draw loop must force alpha=255 on all pixels
- `BI_BITFIELDS` RGBa format is NOT supported by GDI — use `BI_RGB` with
  byte-order B,G,R,A and manual alpha fix

## Debugging History (2026-07-09)

### Problem: Empty/gray window — no text visible

**Root cause**: `OWN_WINDOW` is `#undef` in the MinGW build (`config.h` line 53:
`/* #undef OWN_WINDOW */`). This means `conky.cc` computes `text_start` as
**screen-absolute** coordinates (e.g., x=2180 for a `top_right` window on a
2560-wide primary monitor), not window-relative. The `draw_string_at()`,
`draw_line()`, `draw_rect()`, and `fill_rect()` functions must subtract
`window_rect_.left/top` to convert to client (DIB) coordinates.

**Fix**: Restored `window_rect_` subtraction in all drawing functions:
- `draw_string_at()` — `cx = x - window_rect_.left; cy = y - window_rect_.top;`
- `draw_line()` — subtract from each endpoint
- `draw_rect()` / `fill_rect()` — subtract from x,y

Also added `GetWindowRect()` sync in `begin_draw_text()` and `move_win()`
to ensure `window_rect_` is always current.

### Debugging technique

1. Used a capture program (`capture_conky.c`) with `PrintWindow(hwnd, dc, 3)`
   to save layered window content to BMP
2. Added diagnostic overlay in `end_draw_text()` — blue checkerboard — to
   confirm the DIB was being written and composited
3. Added logging of `window_rect_` coords + incoming draw coordinates to
   detect the screen-absolute vs client mismatch
4. Compared output at each iteration with Linux reference screenshot
