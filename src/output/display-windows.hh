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

#ifndef DISPLAY_WINDOWS_HH
#define DISPLAY_WINDOWS_HH

#include <windows.h>

#include <string>
#include <vector>

#include "../lua/luamm.hh"
#include "display-output.hh"

namespace conky {

/*
 * Windows desktop-layer display output.
 * Renders conky text on the desktop background, behind icons.
 */
class display_output_windows : public display_output_base {
 public:
  explicit display_output_windows(const std::string &name_);

  virtual ~display_output_windows() {}

  // display_output_base overrides
  virtual bool detect();
  virtual bool initialize();
  virtual bool shutdown();

  virtual bool graphical() { return true; }

  // drawing primitives
  virtual void set_foreground_color(Colour c);
  virtual int calc_text_width(const char *s);

  virtual void begin_draw_text();
  virtual void end_draw_text();
  virtual void draw_string(const char *s, int w);
  virtual void draw_string_at(int x, int y, const char *s, int w);
  virtual void line_inner_done() {}

  // shapes
  virtual void set_line_style(int w, bool solid);
  virtual void set_dashes(char *s);
  virtual void draw_line(int x1, int y1, int x2, int y2);
  virtual void draw_rect(int x, int y, int w, int h);
  virtual void fill_rect(int x, int y, int w, int h);

  virtual void move_win(int x, int y);
  virtual void clear_text(int exposures);
  virtual void begin_draw_stuff();
  virtual void end_draw_stuff();

  // font stuff
  virtual int font_height(unsigned int);
  virtual int font_ascent(unsigned int);
  virtual int font_descent(unsigned int);
  virtual void setup_fonts(void);
  virtual void set_font(unsigned int);
  virtual void free_fonts(bool utf8);
  virtual void load_fonts(bool utf8);

  // main loop
  virtual bool main_loop_wait(double t);

 private:
  HWND hwnd_{nullptr};
  HDC hdc_{nullptr};
  HFONT current_font_{nullptr};
  std::vector<HFONT> fonts_;
  int cur_font_index_{0};
  int font_h_{0}, font_a_{0}, font_d_{0};
  RECT window_rect_{};
  Colour current_color_{255, 255, 255};  // default white

  // Memory DC for double-buffered UpdateLayeredWindow rendering.
  // begin_draw_text() creates a 32-bit DIB section (pre-multiplied alpha);
  // end_draw_text() calls UpdateLayeredWindow then cleans up.
  HDC mem_dc_{nullptr};
  HBITMAP mem_bitmap_{nullptr};
  HBITMAP mem_old_bitmap_{nullptr};
  void *mem_bits_{nullptr};
  int win_w_{0}, win_h_{0};

  void resize_to_content();
  bool embed_in_desktop();
  void create_fonts();
  static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
};

}  // namespace conky

#endif /* DISPLAY_WINDOWS_HH */
