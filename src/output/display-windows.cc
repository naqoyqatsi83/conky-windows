/*
 *
 * Conky, a system monitor, based on torsmo
 *
 * Please see COPYING for details
 *
 * Copyright (C) 2024 et al.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <config.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../conky.h"
#include "../logging.h"
#include "../lua/fonts.h"
#include "display-windows.hh"

#ifdef BUILD_GUI
#include "gui.h"
#endif

extern conky::vec2i text_start;  /* text start position in window */
extern conky::vec2i text_offset; /* offset for start position */
extern conky::vec2i
    text_size; /* initially 1 so no zero-sized window is created */
int get_border_total();

/* Stub implementations for X11-only GUI functions */
#ifdef BUILD_GUI
#ifndef BUILD_X11
void print_monitor(struct text_object *, char *p, unsigned int) { if (p) *p = 0; }
void print_monitor_number(struct text_object *, char *p, unsigned int) { if (p) *p = 0; }
void print_desktop(struct text_object *, char *p, unsigned int) { if (p) *p = 0; }
void print_desktop_number(struct text_object *, char *p, unsigned int) { if (p) *p = 0; }
void print_desktop_name(struct text_object *, char *p, unsigned int) { if (p) *p = 0; }
void print_key_num_lock(struct text_object *, char *p, unsigned int) { if (p) *p = 0; }
void print_key_caps_lock(struct text_object *, char *p, unsigned int) { if (p) *p = 0; }
void print_key_scroll_lock(struct text_object *, char *p, unsigned int) { if (p) *p = 0; }
void print_keyboard_layout(struct text_object *, char *p, unsigned int) { if (p) *p = 0; }
void print_mouse_speed(struct text_object *, char *p, unsigned int) { if (p) *p = 0; }
#endif /* !BUILD_X11 */
#endif /* BUILD_GUI */

/* Forward declarations from conky.cc */
void update_text();

namespace conky {
namespace {

display_output_windows windows_output("windows");

//

}  // namespace

simple_config_setting<bool> out_to_windows("out_to_windows", true, false);

#ifdef BUILD_WINDOWS
template <>
void register_output<output_t::WINDOWS>(display_outputs_t &outputs) {
  outputs.push_back(&windows_output);
}
#endif

display_output_windows::display_output_windows(const std::string &name_)
    : display_output_base(name_) {}

LRESULT CALLBACK display_output_windows::wnd_proc(HWND hwnd, UINT msg,
                                                   WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_PAINT: {
      PAINTSTRUCT ps;
      BeginPaint(hwnd, &ps);
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProc(hwnd, msg, wp, lp);
}

bool display_output_windows::embed_in_desktop() {
  const char CLASS_NAME[] = "ConkyDesktop";
  HINSTANCE inst = GetModuleHandle(nullptr);

  WNDCLASSEX wc = {};
  wc.cbSize = sizeof(WNDCLASSEX);
  wc.lpfnWndProc = display_output_windows::wnd_proc;
  wc.hInstance = inst;
  wc.lpszClassName = CLASS_NAME;
  wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  RegisterClassEx(&wc);

  // Use the primary monitor only (not the full virtual-screen span) so that
  // conky's alignment math (text_offset + goto offsets) stays within bounds.
  // On a multi-monitor setup, the virtual screen would be much wider and
  // "top_right" alignment would push text off the right edge.
  HMONITOR hmon =
      MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO mi = {};
  mi.cbSize = sizeof(mi);
  if (!GetMonitorInfo(hmon, &mi)) {
    LOG_ERROR("windows display: GetMonitorInfo failed");
    return false;
  }

  int mon_left = mi.rcMonitor.left;
  int mon_top = mi.rcMonitor.top;
  int mon_w = mi.rcMonitor.right - mi.rcMonitor.left;
  int mon_h = mi.rcMonitor.bottom - mi.rcMonitor.top;

  // Use workarea height (excludes taskbar) to avoid drawing behind it.
  RECT workarea_rect = mi.rcWork;
  int win_h = workarea_rect.bottom - mon_top;
  if (win_h <= 0 || win_h > mon_h) { win_h = mon_h; }

  // Create a small initial window; resize_to_content() will resize and
  // reposition it once the first layout pass completes.  Using a full-monitor
  // initial size would cause a visible full-screen flash.
  int init_w = 320, init_h = 200;
  hwnd_ = CreateWindowEx(
      WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
      CLASS_NAME, "Conky",
      WS_POPUP | WS_VISIBLE | WS_CLIPSIBLINGS,
      mon_left, mon_top, init_w, init_h,
      nullptr, nullptr, inst, nullptr);

  if (hwnd_ == nullptr) {
    LOG_ERROR("windows display: CreateWindowEx failed: {}", (int)GetLastError());
    return false;
  }

  // Keep default Z-order (so the window appears above the desktop wallpaper).
  GetWindowRect(hwnd_, &window_rect_);

  LOG_INFO("windows display: created initial window {}x{} @ ({},{})", init_w, init_h,
           mon_left, mon_top);
  return true;
}

void display_output_windows::create_fonts() {
  /* Free any previously-created fonts first */
  for (auto f : fonts_) {
    if (f) DeleteObject(f);
  }
  fonts_.clear();
  current_font_ = nullptr;

  HDC dc = GetDC(hwnd_);
  int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSY) : 96;
  if (dc) ReleaseDC(hwnd_, dc);

  /* Create one HFONT per entry in the global fonts vector */
  for (const auto &entry : fonts) {
    std::string fontspec = entry.name;
    std::string font_name = "Consolas";
    int font_size = 12;
    int font_weight = FW_NORMAL;

    if (!fontspec.empty() && fontspec != "6x10") {
      /* Extract everything before the first : as the face name */
      auto first_colon = fontspec.find(':');
      if (first_colon != std::string::npos) {
        font_name = fontspec.substr(0, first_colon);
      } else {
        font_name = fontspec;
      }

      /* Check for :bold / :italic flags */
      if (fontspec.find(":bold") != std::string::npos) {
        font_weight = FW_BOLD;
      }

      /* Extract :size=N */
      auto size_pos = fontspec.find(":size=");
      if (size_pos != std::string::npos) {
        font_size = std::stoi(fontspec.substr(size_pos + 6));
      }
    }

    int lf_height = -MulDiv(font_size, dpi, 72);

    HFONT hf = CreateFontA(
        lf_height, 0, 0, 0, font_weight, FALSE, FALSE, FALSE, ANSI_CHARSET,
        OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        FIXED_PITCH | FF_MODERN, font_name.c_str());

    if (hf == nullptr) {
      /* Fallback: Consolas at the configured size, normal weight */
      hf = CreateFontA(
          lf_height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
          OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
          FIXED_PITCH | FF_MODERN, "Consolas");
    }

    if (hf == nullptr) {
      hf = (HFONT)GetStockObject(ANSI_FIXED_FONT);
    }

    fonts_.push_back(hf);
  }

  if (fonts_.empty()) {
    fonts_.push_back((HFONT)GetStockObject(ANSI_FIXED_FONT));
  }

  current_font_ = fonts_[0];

  /* Get metrics for the first font */
  HDC dc2 = GetDC(hwnd_);
  if (dc2 != nullptr) {
    HFONT old = (HFONT)SelectObject(dc2, fonts_[0]);
    TEXTMETRIC tm;
    GetTextMetrics(dc2, &tm);
    font_h_ = tm.tmHeight;
    font_a_ = tm.tmAscent;
    font_d_ = tm.tmDescent;
    SelectObject(dc2, old);
    ReleaseDC(hwnd_, dc2);
  }
}

int display_output_windows::font_height(unsigned int) { return font_h_; }

int display_output_windows::font_ascent(unsigned int) { return font_a_; }

int display_output_windows::font_descent(unsigned int) { return font_d_; }

void display_output_windows::setup_fonts(void) {
  if (fonts_.empty()) { load_fonts(false); }
}

void display_output_windows::set_font(unsigned int idx) {
  if (idx < fonts_.size()) {
    cur_font_index_ = idx;
    current_font_ = fonts_[idx];

    /* Update font metrics for this font */
    HDC dc = GetDC(hwnd_);
    if (dc != nullptr) {
      HFONT old = (HFONT)SelectObject(dc, current_font_);
      TEXTMETRIC tm;
      GetTextMetrics(dc, &tm);
      font_h_ = tm.tmHeight;
      font_a_ = tm.tmAscent;
      font_d_ = tm.tmDescent;
      SelectObject(dc, old);
      ReleaseDC(hwnd_, dc);
    }
  }
}

void display_output_windows::free_fonts(bool) {
  for (auto f : fonts_) {
    if (f) DeleteObject(f);
  }
  fonts_.clear();
  current_font_ = nullptr;
}

void display_output_windows::load_fonts(bool) { create_fonts(); }

bool display_output_windows::detect() {
  if (out_to_windows.get(*state)) {
    LOG_DEBUG("display output '{}' enabled in config", name);
    return true;
  }
  return false;
}

bool display_output_windows::initialize() {
  if (!embed_in_desktop()) { return false; }
  create_fonts();
  is_graphical = true;

  /* Workarea describes the primary monitor's usable area (excluding taskbar).
   * Conky positions text relative to this rect (e.g. "top_right" means the
   * text sits at the right edge of the workarea).  The window is resized and
   * positioned by resize_to_content() using the alignment + gap settings. */
  HMONITOR hmon = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO mi = {};
  mi.cbSize = sizeof(mi);
  if (GetMonitorInfo(hmon, &mi)) {
    workarea = absolute_rect<int>(
        vec2<int>(mi.rcWork.left, mi.rcWork.top),
        vec2<int>(mi.rcWork.right, mi.rcWork.bottom));
  } else {
    /* Fallback: use the window rect */
    RECT wr;
    GetWindowRect(hwnd_, &wr);
    workarea = absolute_rect<int>(vec2<int>(wr.left, wr.top),
                                  vec2<int>(wr.right, wr.bottom));
  }

  return true;
}

bool display_output_windows::shutdown() {
  free_fonts(false);
  if (hwnd_ != nullptr) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
  return true;
}

void display_output_windows::set_foreground_color(Colour c) {
  current_color_ = c;
}

int display_output_windows::calc_text_width(const char *s) {
  if (s == nullptr || *s == '\0') return 0;
  // Acquire a DC if called outside the draw phase (update_text_area →
  // text_size_updater). Without this, text widths return 0 and the layout
  // engine gets no useful size data from any string.
  HDC dc = hdc_;
  bool own_dc = false;
  if (dc == nullptr) {
    dc = GetDC(hwnd_);
    own_dc = true;
  }
  if (dc == nullptr) return 0;
  SIZE sz{};
  HFONT old = (HFONT)SelectObject(dc, current_font_);
  GetTextExtentPoint32A(dc, s, (int)strlen(s), &sz);
  SelectObject(dc, old);
  if (own_dc) ReleaseDC(hwnd_, dc);
  return sz.cx;
}

void display_output_windows::begin_draw_text() {
  if (hwnd_ == nullptr) return;

  /* Sync window_rect_ for screen→client coordinate conversion */
  GetWindowRect(hwnd_, &window_rect_);

  /* Get current client dimensions — the window may have been resized. */
  RECT client;
  GetClientRect(hwnd_, &client);
  win_w_ = client.right - client.left;
  win_h_ = client.bottom - client.top;
  if (win_w_ <= 0 || win_h_ <= 0) return;

  /* Create a 32-bit DIB section for per-pixel alpha composition.
   * GDI draws the RGB channels; we fix alpha ourselves in end_draw_text(). */
  BITMAPINFO bmi = {};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = win_w_;
  bmi.bmiHeader.biHeight = -win_h_; /* top-down */
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  HDC screen_dc = GetDC(nullptr);
  if (screen_dc == nullptr) return;
  mem_dc_ = CreateCompatibleDC(screen_dc);
  if (mem_dc_ == nullptr) {
    ReleaseDC(nullptr, screen_dc);
    return;
  }
  mem_bitmap_ = CreateDIBSection(mem_dc_, &bmi, DIB_RGB_COLORS, &mem_bits_,
                                  nullptr, 0);
  ReleaseDC(nullptr, screen_dc);
  if (mem_bitmap_ == nullptr) {
    DeleteDC(mem_dc_);
    mem_dc_ = nullptr;
    return;
  }

  /* Select the DIB into the memory DC, saving the old (1x1 mono) bitmap */
  SelectObject(mem_dc_, mem_bitmap_);

  /* Initialize every pixel to ARGB(0,0,0,0) — fully transparent black */
  memset(mem_bits_, 0, (size_t)win_w_ * win_h_ * 4);

  /* No background fill — the DIB starts fully transparent. GDI draws text,
   * bars, and graphs directly onto this transparent canvas. In end_draw_text,
   * any pixel whose RGB channels are non-zero (i.e., GDI touched it) gets
   * alpha=255; untouched pixels stay at ARGB(0,0,0,0) — fully transparent. */

  /* Set up text-rendering state for subsequent draw_string / draw_string_at
   * calls.  hdc_ points at the mem DC so that existing drawing code works
   * unchanged: it selects fonts, sets text colour, and calls TextOutA. */
  LOG_INFO("windows display: begin_draw_text DIB={}x{} window_rect=({},{})-({},{})",
           win_w_, win_h_, window_rect_.left, window_rect_.top, window_rect_.right, window_rect_.bottom);

  hdc_ = mem_dc_;
  SetBkMode(hdc_, TRANSPARENT);
  SetBkColor(hdc_, RGB(0, 0, 0));

  /* Select the current font into the mem DC */
  if (current_font_ != nullptr) {
    SelectObject(hdc_, current_font_);
  }
}

void display_output_windows::end_draw_text() {
  if (mem_dc_ == nullptr || mem_bits_ == nullptr) {
    if (hdc_ != nullptr && hdc_ != mem_dc_) {
      ReleaseDC(hwnd_, hdc_);
    }
    hdc_ = nullptr;
    return;
  }

  /* Apply per-pixel alpha: any pixel GDI touched (non-zero RGB) gets full
   * opacity; untouched pixels stay at ARGB(0,0,0,0) — fully transparent.
   * UpdateLayeredWindow with AC_SRC_ALPHA expects pre-multiplied alpha, so
   * pixels with alpha=0 must also have RGB=0 (which they already do from the
   * initial memset in begin_draw_text). */
  unsigned char *pixels = static_cast<unsigned char *>(mem_bits_);
  int total = win_w_ * win_h_;
  for (int i = 0; i < total; i++) {
    if (pixels[i * 4 + 0] != 0 || pixels[i * 4 + 1] != 0 ||
        pixels[i * 4 + 2] != 0) {
      pixels[i * 4 + 3] = 255;
    }
  }

  /* --- Submit to UpdateLayeredWindow with per-pixel alpha --- */
  POINT pt_src = {0, 0};
  SIZE sz = {win_w_, win_h_};
  BLENDFUNCTION bf = {};
  bf.BlendOp = AC_SRC_OVER;
  bf.SourceConstantAlpha = 255;
  bf.AlphaFormat = AC_SRC_ALPHA;

  POINT pt_dst;
  pt_dst.x = window_rect_.left;
  pt_dst.y = window_rect_.top;

  UpdateLayeredWindow(hwnd_, nullptr, &pt_dst, &sz, mem_dc_, &pt_src, 0, &bf,
                      ULW_ALPHA);

  DeleteDC(mem_dc_);
  DeleteObject(mem_bitmap_);
  mem_dc_ = nullptr;
  mem_bitmap_ = nullptr;
  mem_bits_ = nullptr;
  hdc_ = nullptr;
}

/* Converts a UTF-8 string to the system ANSI codepage and calls TextOutA.
   This ensures characters like ° (UTF-8 0xC2 0xB0) render correctly instead
   of being misinterpreted as two ANSI characters (Â°). */
static void text_out_utf8(HDC hdc, int x, int y, const char *s) {
  if (s == nullptr || *s == '\0') return;

  /* Fast path: if all bytes are ASCII (0x00-0x7F), no conversion needed */
  bool has_non_ascii = false;
  for (const char *p = s; *p && !has_non_ascii; p++) {
    if (static_cast<unsigned char>(*p) >= 0x80) has_non_ascii = true;
  }
  if (!has_non_ascii) {
    TextOutA(hdc, x, y, s, (int)strlen(s));
    return;
  }

  /* Convert UTF-8 → wide → ANSI codepage */
  int wide_len = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
  if (wide_len <= 0) { TextOutA(hdc, x, y, s, (int)strlen(s)); return; }

  std::vector<wchar_t> wide(wide_len);
  MultiByteToWideChar(CP_UTF8, 0, s, -1, wide.data(), wide_len);

  int ansi_len = WideCharToMultiByte(CP_ACP, 0, wide.data(), -1, nullptr, 0, nullptr, nullptr);
  if (ansi_len <= 0) { TextOutA(hdc, x, y, s, (int)strlen(s)); return; }

  std::vector<char> ansi(ansi_len);
  WideCharToMultiByte(CP_ACP, 0, wide.data(), -1, ansi.data(), ansi_len, nullptr, nullptr);
  TextOutA(hdc, x, y, ansi.data(), ansi_len - 1);
}

void display_output_windows::draw_string(const char *s, int) {
  if (hdc_ == nullptr || s == nullptr) return;
  HFONT old = (HFONT)SelectObject(hdc_, current_font_);
  SetTextColor(hdc_, RGB(current_color_.red, current_color_.green,
                         current_color_.blue));
  /* Note: draw_string draws at (0,0) on the current line, not absolute (0,0) */
  text_out_utf8(hdc_, 0, 0, s);
  SelectObject(hdc_, old);
}

void display_output_windows::draw_string_at(int x, int y, const char *s, int) {
  if (hdc_ == nullptr || s == nullptr) return;
  int cx = x - window_rect_.left;
  int cy = y - window_rect_.top;
  HFONT old = (HFONT)SelectObject(hdc_, current_font_);
  SetTextColor(hdc_, RGB(current_color_.red, current_color_.green, current_color_.blue));
  text_out_utf8(hdc_, cx, cy, s);
  SelectObject(hdc_, old);
}

void display_output_windows::set_line_style(int w, bool solid) {
  (void)w;
  (void)solid;
}

void display_output_windows::set_dashes(char *s) { (void)s; }

void display_output_windows::draw_line(int x1, int y1, int x2, int y2) {
  if (hdc_ == nullptr) return;
  HPEN pen = CreatePen(PS_SOLID, 1,
      RGB(current_color_.red, current_color_.green, current_color_.blue));
  HPEN old = (HPEN)SelectObject(hdc_, pen);
  MoveToEx(hdc_, x1 - window_rect_.left, y1 - window_rect_.top, nullptr);
  LineTo(hdc_, x2 - window_rect_.left, y2 - window_rect_.top);
  SelectObject(hdc_, old);
  DeleteObject(pen);
}

void display_output_windows::draw_rect(int x, int y, int w, int h) {
  if (hdc_ == nullptr) return;
  int cx = x - window_rect_.left;
  int cy = y - window_rect_.top;
  HPEN pen = CreatePen(PS_SOLID, 1,
      RGB(current_color_.red, current_color_.green, current_color_.blue));
  HGDIOBJ old_pen = SelectObject(hdc_, pen);
  HGDIOBJ old_brush = SelectObject(hdc_, GetStockObject(NULL_BRUSH));
  Rectangle(hdc_, cx, cy, cx + w, cy + h);
  SelectObject(hdc_, old_brush);
  SelectObject(hdc_, old_pen);
  DeleteObject(pen);
}

void display_output_windows::fill_rect(int x, int y, int w, int h) {
  if (hdc_ == nullptr) return;
  int cx = x - window_rect_.left;
  int cy = y - window_rect_.top;
  RECT r = {cx, cy, cx + w, cy + h};
  HBRUSH br = CreateSolidBrush(
      RGB(current_color_.red, current_color_.green, current_color_.blue));
  FillRect(hdc_, &r, br);
  DeleteObject(br);
}

void display_output_windows::move_win(int x, int y) {
  if (hwnd_ == nullptr) return;
  SetWindowPos(hwnd_, nullptr, x, y, 0, 0,
               SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  GetWindowRect(hwnd_, &window_rect_);
}

void display_output_windows::clear_text(int) {
  /* No-op: begin_draw_text() creates a fresh transparent bitmap each frame,
   * so old content is never carried over.  Direct GDI drawing on layered
   * windows is not visible under DWM on Win8+. */
}

void display_output_windows::begin_draw_stuff() {}

void display_output_windows::end_draw_stuff() {}

void display_output_windows::resize_to_content() {
  if (hwnd_ == nullptr) return;

  /* text_size starts at (1,1) as a sentinel; don't resize until the first
   * real layout has been computed by update_text_area(). */
  if (text_size.x() <= 1 && text_size.y() <= 1) return;

  int border = get_border_total();

  /* Desired window size */
  int desired_w = text_size.x() + 2 * border;
  int desired_h = text_size.y() + 2 * border;
  if (desired_w < 16) desired_w = 16;
  if (desired_h < 16) desired_h = 16;

  /* Get the monitor this window is on for workarea-correct positioning */
  HMONITOR hmon = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
  MONITORINFO mi = {};
  mi.cbSize = sizeof(mi);
  if (!GetMonitorInfo(hmon, &mi)) {
    hmon = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
    GetMonitorInfo(hmon, &mi);
  }
  int max_h = mi.rcWork.bottom - mi.rcWork.top;
  if (desired_h > max_h) desired_h = max_h;

  /* Compute the aligned window position on the desktop.
   * This mirrors the logic in update_text_area() which computes xy for X11; on
   * Windows we own the window position ourselves. */
  {
    alignment align = text_alignment.get(*state);
    int gap_x_val = dpi_scale(gap_x.get(*state));
    int gap_y_val = dpi_scale(gap_y.get(*state));

    int desired_x, desired_y;

    switch (vertical_alignment(align)) {
      case axis_align::START:
        desired_y = mi.rcWork.top + gap_y_val;
        break;
      case axis_align::END:
      default:
        desired_y = mi.rcWork.bottom - desired_h - gap_y_val;
        break;
      case axis_align::MIDDLE:
        desired_y =
            mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top) / 2 -
            desired_h / 2;
        break;
    }

    switch (horizontal_alignment(align)) {
      case axis_align::START:
      default:
        desired_x = mi.rcWork.left + gap_x_val;
        break;
      case axis_align::END:
        desired_x = mi.rcWork.right - desired_w - gap_x_val;
        break;
      case axis_align::MIDDLE:
        desired_x =
            mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left) / 2 -
            desired_w / 2;
        break;
    }

    /* Get current geometry to see if anything changed */
    RECT cur;
    GetWindowRect(hwnd_, &cur);
    bool same_pos = (cur.left == desired_x && cur.top == desired_y);
    bool same_size = ((cur.right - cur.left) == desired_w &&
                      (cur.bottom - cur.top) == desired_h);
    if (same_pos && same_size) return;

    SetWindowPos(hwnd_, nullptr, desired_x, desired_y, desired_w, desired_h,
                 SWP_NOZORDER | SWP_NOACTIVATE);
  }

  /* Keep window_rect_ in sync for ULW window-position parameter */
  GetWindowRect(hwnd_, &window_rect_);
}

bool display_output_windows::main_loop_wait(double t) {
  MSG msg;
  DWORD timeout_ms = (DWORD)std::max(t * 1000, 10.0);
  DWORD start = GetTickCount();

  while (true) {
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
      if (msg.message == WM_QUIT) return false;
    }

    DWORD elapsed = GetTickCount() - start;
    if (elapsed >= timeout_ms) {
      update_text();
      update_text_area();

      /* Resize the window to match the content now that update_text_area()
       * has computed text_start/text_size.  On the first call this shrinks
       * the full-monitor window to just the content area; subsequent calls
       * are no-ops when the size hasn't changed. */
      resize_to_content();

      draw_stuff();
      break;
    }

    Sleep(10);
  }
  return true;
}

}  // namespace conky
