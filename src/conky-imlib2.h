/*
 *
 * Conky, a system monitor, based on torsmo
 *
 * Please see COPYING for details
 *
 * Copyright (c) 2005-2024 Brenden Matthews, et. al.
 * All rights reserved.
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

#ifndef _CONKY_IMBLI2_H_
#define _CONKY_IMBLI2_H_

#include "content/text_object.h"
#include "lua/setting.hh"

#include <array>

#ifndef _WIN32
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wvariadic-macros"
#include <X11/Xlib.h>
#pragma GCC diagnostic pop
#else
#include <windows.h>
#endif /* _WIN32 */

using saved_coordinates_t = std::array<std::array<int, 2>, 100>;
extern saved_coordinates_t saved_coordinates;

void cimlib_add_image(const char *args);
void cimlib_set_cache_size(long size);
void cimlib_set_cache_flush_interval(long interval);
void cimlib_render(int x, int y, int width, int height, uint32_t flush_interval,
                   bool draw_blended);
void cimlib_cleanup(void);

/// Creates the imlib context and binds it to the X display/visual/colormap/
/// drawable. Call once, after the X window exists.
void cimlib_init();
/// Tears down the imlib context and cached images.
void cimlib_deinit();

#ifdef _WIN32
/// Windows only: draws every configured ${image} directly onto `hdc` (this
/// port's per-frame DIB, see display-windows.cc's begin_draw_text()) --
/// called from there on every frame, the same way composite_hook_surface()
/// re-composites the Lua draw-hook layer every frame. Coordinates are
/// window-relative (screen-absolute minus window_left/window_top), matching
/// this port's established convention (see AGENTS.md).
void cimlib_draw_windows(HDC hdc, int win_w, int win_h, int window_left,
                         int window_top);
#endif /* _WIN32 */

void print_image_callback(struct text_object *, char *, unsigned int);

extern conky::range_config_setting<unsigned int> imlib_cache_flush_interval;
extern conky::simple_config_setting<bool> imlib_draw_blended;

#endif /* _CONKY_IMBLI2_H_ */
