/*
 *
 * Conky, a system monitor, based on torsmo
 *
 * Please see COPYING for details
 *
 * Copyright (c) 2005-2025 Brenden Matthews, et. al. (see AUTHORS) All rights
 * reserved.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details. You should have received a copy of the
 * GNU General Public License along with this program. If not, see
 * <http://www.gnu.org/licenses/>.
 *
 */

// Cairo, resolved dynamically at runtime on Windows.
//
// conky.exe itself does not link against cairo.dll (see
// cmake/ConkyPlatformChecks.cmake's OS_WINDOWS branch of BUILD_LUA_CAIRO):
// doing so would make cairo.dll a hard PE import and prevent conky.exe from
// starting at all if it's ever missing, the same mistake already fixed once
// for NVML (see src/data/hardware/nvidia_nvml.cc, conky-windows issue #4).
// This is the shared LoadLibraryA/GetProcAddress facility for the Windows
// call sites that need real Cairo calls despite that:
// display_output_windows's Lua-draw-hook surface (src/output/
// display-windows.cc) and llua_update_window_table()'s device-scale query
// (src/lua/llua.cc) -- see conky-windows issue #10.
//
// A plain ARGB32 image surface is used rather than a win32/HDC-backed
// surface: this port's mem_dc_ (the DIB UpdateLayeredWindow submits every
// frame) is created and fully destroyed inside every single
// begin_draw_text()/end_draw_text() call -- see display-windows.cc -- and
// llua_draw_pre_hook()/llua_draw_post_hook() both fire *outside* that
// window (conky.cc's draw_stuff()), so there is no live HDC at the moment
// Lua's draw hook runs. A persistent, HDC-independent image surface sidesteps
// that lifetime mismatch entirely; display_output_windows composites it into
// each frame's DIB itself (see begin_draw_text()).
#ifndef _CAIRO_DYNAMIC_HH
#define _CAIRO_DYNAMIC_HH

#include <cairo.h>
#include <windows.h>

namespace conky {
namespace cairo_dyn {

// Attempts to load cairo.dll and resolve every symbol below, if not already
// attempted. Safe to call repeatedly. Returns whether it's usable.
bool available();

// Thin wrappers around the dynamically-resolved symbols. Each is a no-op
// (returning nullptr / doing nothing / returning a value indicating "no
// content") if available() is false -- callers don't need to guard every
// call individually.
cairo_surface_t *image_surface_create(int width, int height);
cairo_surface_t *surface_reference(cairo_surface_t *surface);
void surface_destroy(cairo_surface_t *surface);
void surface_flush(cairo_surface_t *surface);
unsigned char *image_surface_get_data(cairo_surface_t *surface);
int image_surface_get_stride(cairo_surface_t *surface);
void surface_mark_dirty(cairo_surface_t *surface);
void surface_get_device_scale(cairo_surface_t *surface, double *x,
                              double *y);

}  // namespace cairo_dyn
}  // namespace conky

#endif /* _CAIRO_DYNAMIC_HH */
