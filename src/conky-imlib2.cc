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

#include "conky-imlib2.h"

#include "common.h"
#include "conky.h"
#include "content/text_object.h"
#include "logging.h"
#include "output/display-output.hh"

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#ifndef _WIN32
#include <Imlib2.h>
#include "lua/x11-settings.h"
#include "output/x11.h"
#endif /* _WIN32 */

struct image_list_s {
  char name[1024];
#ifndef _WIN32
  Imlib_Image image;
#endif /* _WIN32 */
  int x, y, w, h;
  int wh_set;
  char no_cache;
  int flush_interval;
  struct image_list_s *next;
};

struct image_list_s *image_list_start, *image_list_end;
std::array<std::array<int, 2>, 100> saved_coordinates;

conky::range_config_setting<unsigned int> imlib_cache_flush_interval(
    "imlib_cache_flush_interval", 0, std::numeric_limits<unsigned int>::max(),
    0, true);

conky::simple_config_setting<bool> imlib_draw_blended("draw_blended", true,
                                                      true);

conky::range_config_setting<unsigned long> imlib_cache_size(
    "imlib_cache_size", 0, std::numeric_limits<unsigned long>::max(),
    4096 * 1024, true);

#ifndef _WIN32

/* areas to update */
Imlib_Updates updates, current_update;
/* our virtual framebuffer image we draw into */
Imlib_Image buffer, image;

namespace {
Imlib_Context context;

unsigned int cimlib_cache_flush_last = 0;
}  // namespace

void cimlib_init() {
  if (display == nullptr || window.visual == nullptr) { return; }

  image_list_start = image_list_end = nullptr;
  context = imlib_context_new();
  imlib_context_push(context);
  imlib_set_cache_size(imlib_cache_size.get(*state));
  /* set the maximum number of colors to allocate for 8bpp and less to 256 */
  imlib_set_color_usage(256);
  /* dither for depths < 24bpp */
  imlib_context_set_dither(1);
  /* set the display , visual, colormap and drawable we are using */
  imlib_context_set_display(display);
  imlib_context_set_visual(window.visual);
  imlib_context_set_colormap(window.colourmap);
  imlib_context_set_drawable(window.drawable);
}

void cimlib_deinit() {
  if (context == nullptr) { return; }
  cimlib_cleanup();
  imlib_context_disconnect_display();
  imlib_context_pop();
  imlib_context_free(context);
  context = nullptr;
}

#else /* _WIN32 */

#include <gdiplus.h>
#include <unordered_map>

namespace {
/* Per-path decoded-image cache, mirroring Imlib2's own internal cache on
 * the Linux side (imlib_load_image() is cheap on a cache hit there; this
 * is the equivalent for Gdiplus::Bitmap, keyed the same way -- by path). */
struct CachedImage {
  std::unique_ptr<Gdiplus::Bitmap> bitmap;
  time_t loaded_at = 0;
};
std::unordered_map<std::string, CachedImage> g_image_cache;

ULONG_PTR g_gdiplus_token = 0;
bool g_gdiplus_started = false;

/* cimlib_render()'s x/y are the same screen-absolute origin (text_start)
 * every ${image} coordinate is relative to -- captured here so
 * cimlib_draw_windows() (called later, from begin_draw_text(), which has
 * no access to text_start) can reproduce it. */
int g_render_origin_x = 0, g_render_origin_y = 0;
bool g_draw_blended = true;
unsigned int g_cimlib_cache_flush_last = 0;

std::wstring to_wide(const char *utf8) {
  if (utf8 == nullptr || utf8[0] == '\0') { return L""; }
  int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
  if (len <= 0) { return L""; }
  std::wstring wide(static_cast<size_t>(len) - 1, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &wide[0], len);
  return wide;
}

/* Returns the cached bitmap for `path`, (re)loading it from disk if it's
 * not cached yet or `force_reload` says the caller's no_cache/
 * flush_interval settings require a fresh decode this frame. nullptr on
 * load failure (bad path, unsupported format, etc). */
Gdiplus::Bitmap *get_cached_image(const std::string &path, bool force_reload) {
  auto it = g_image_cache.find(path);
  if (it != g_image_cache.end() && !force_reload) {
    return it->second.bitmap.get();
  }

  std::wstring wpath = to_wide(path.c_str());
  auto bitmap = std::make_unique<Gdiplus::Bitmap>(wpath.c_str());
  if (bitmap->GetLastStatus() != Gdiplus::Ok) {
    static bool reported = false;
    if (!reported) { LOG_ERROR("unable to load image '{}'", path); }
    reported = true;
    g_image_cache.erase(path);
    return nullptr;
  }

  CachedImage entry;
  entry.bitmap = std::move(bitmap);
  entry.loaded_at = time(nullptr);
  Gdiplus::Bitmap *raw = entry.bitmap.get();
  g_image_cache[path] = std::move(entry);
  return raw;
}
}  // namespace

void cimlib_init() {
  if (g_gdiplus_started) { return; }
  Gdiplus::GdiplusStartupInput input;
  if (Gdiplus::GdiplusStartup(&g_gdiplus_token, &input, nullptr) ==
      Gdiplus::Ok) {
    g_gdiplus_started = true;
  }
}

void cimlib_deinit() {
  if (!g_gdiplus_started) { return; }
  cimlib_cleanup();
  g_image_cache.clear();
  Gdiplus::GdiplusShutdown(g_gdiplus_token);
  g_gdiplus_started = false;
}

#endif /* _WIN32 */

void cimlib_cleanup() {
  struct image_list_s *cur = image_list_start, *last = nullptr;
  while (cur != nullptr) {
    last = cur;
    cur = last->next;
    delete last;
  }
  image_list_start = image_list_end = nullptr;
}

void cimlib_add_image(const char *args) {
  struct image_list_s *cur = nullptr;
  const char *tmp;

  cur = new struct image_list_s[sizeof(struct image_list_s)];
  memset(cur, 0, sizeof(struct image_list_s));

  if (sscanf(args, "%1023s", cur->name) == 0) {
    LOG_ERROR(
        "invalid args for $image, format is '<path to image> (-p x,y) (-s WxH) "
        "(-n) (-f interval)' (got '{}')",
        args);
    delete[] cur;
    return;
  }
  strncpy(cur->name, to_real_path(cur->name).string().c_str(), 1024);
  cur->name[1023] = 0;
  //
  // now we check for optional args
  tmp = strstr(args, "-p ");
  if (tmp != nullptr) {
    tmp += 3;
    sscanf(tmp, "%i,%i", &cur->x, &cur->y);
    cur->x = dpi_scale(cur->x);
    cur->y = dpi_scale(cur->y);
  }
  tmp = strstr(args, "-s ");
  if (tmp != nullptr) {
    tmp += 3;
    if (sscanf(tmp, "%ix%i", &cur->w, &cur->h) != 0) { cur->wh_set = 1; }
    cur->w = dpi_scale(cur->w);
    cur->h = dpi_scale(cur->h);
  }

  tmp = strstr(args, "-n");
  if (tmp != nullptr) { cur->no_cache = 1; }

  tmp = strstr(args, "-f ");
  if (tmp != nullptr) {
    tmp += 3;
    if (sscanf(tmp, "%d", &cur->flush_interval) != 0) { cur->no_cache = 0; }
  }
  tmp = strstr(args, "-i ");
  if (tmp != nullptr) {
    tmp += 3;
    int i;
    if (sscanf(tmp, "%d", &i) == 1) {
      const auto &coordinates = saved_coordinates.at(static_cast<size_t>(i));
      cur->x = coordinates[0];
      cur->y = coordinates[1];
    }
  }
  if (cur->flush_interval < 0) {
    LOG_WARNING("flush interval should be >= 0, got {}", cur->flush_interval);
    cur->flush_interval = 0;
  }

  if (image_list_end != nullptr) {
    image_list_end->next = cur;
    image_list_end = cur;
  } else {
    image_list_start = image_list_end = cur;
  }
}

#ifndef _WIN32
static void cimlib_draw_image(struct image_list_s *cur, int *clip_x,
                              int *clip_y, int *clip_x2, int *clip_y2) {
  int w, h;
  time_t now = time(nullptr);
  static int rep = 0;

  if (imlib_context_get_drawable() != window.drawable) {
    imlib_context_set_drawable(window.drawable);
  }

  image = imlib_load_image(cur->name);
  if (image == nullptr) {
    if (rep == 0) { LOG_ERROR("unable to load image '{}'", cur->name); }
    rep = 1;
    return;
  }
  rep = 0; /* reset so disappearing images are reported */

  LOG_DEBUG(
      "drawing image '{}' at ({},{}) scaled to {}x{}, cache interval {} "
      "(no_cache {})",
      cur->name, cur->x, cur->y, cur->w, cur->h, cur->flush_interval,
      cur->no_cache);

  imlib_context_set_image(image);
  /* turn alpha channel on */
  imlib_image_set_has_alpha(1);
  w = imlib_image_get_width();
  h = imlib_image_get_height();
  if (cur->wh_set == 0) {
    cur->w = dpi_scale(w);
    cur->h = dpi_scale(h);
  }
  imlib_context_set_image(buffer);
  imlib_blend_image_onto_image(image, 1, 0, 0, w, h, cur->x, cur->y, cur->w,
                               cur->h);
  imlib_context_set_image(image);
  if ((cur->no_cache != 0) ||
      ((cur->flush_interval != 0) && now % cur->flush_interval == 0)) {
    imlib_free_image_and_decache();
  } else {
    imlib_free_image();
  }
  if (cur->x < *clip_x) { *clip_x = cur->x; }
  if (cur->y < *clip_y) { *clip_y = cur->y; }
  if (cur->x + cur->w > *clip_x2) { *clip_x2 = cur->x + cur->w; }
  if (cur->y + cur->h > *clip_y2) { *clip_y2 = cur->y + cur->h; }
}

static void cimlib_draw_all(int *clip_x, int *clip_y, int *clip_x2,
                            int *clip_y2) {
  struct image_list_s *cur = image_list_start;
  while (cur != nullptr) {
    cimlib_draw_image(cur, clip_x, clip_y, clip_x2, clip_y2);
    cur = cur->next;
  }
}

void cimlib_render(int x, int y, int width, int height, uint32_t flush_interval,
                   bool draw_blended) {
  int clip_x = INT_MAX, clip_y = INT_MAX;
  int clip_x2 = 0, clip_y2 = 0;
  time_t now;

  if (image_list_start == nullptr) {
    return; /* are we actually drawing anything? */
  }

  /* cheque if it's time to flush our cache */
  now = time(nullptr);
  if ((flush_interval != 0u) &&
      now - flush_interval > cimlib_cache_flush_last) {
    int size = imlib_get_cache_size();
    imlib_set_cache_size(0);
    imlib_set_cache_size(size);
    cimlib_cache_flush_last = now;
    LOG_DEBUG("flushing imlib2 cache ({})", now);
  }

  /* take all the little rectangles to redraw and merge them into
   * something sane for rendering */
  buffer = imlib_create_image(width, height);
  /* clear our buffer */
  imlib_context_set_image(buffer);
  imlib_image_clear();

  /* check if we should blend when rendering */
  if (draw_blended) {
    /* we can blend stuff now */
    imlib_context_set_blend(1);
  } else {
    imlib_context_set_blend(0);
  }

  /* turn alpha channel on */
  imlib_image_set_has_alpha(1);

  cimlib_draw_all(&clip_x, &clip_y, &clip_x2, &clip_y2);

  /* set the buffer image as our current image */
  imlib_context_set_image(buffer);

  /* setup our clip rect */
  if (clip_x == INT_MAX) { clip_x = 0; }
  if (clip_y == INT_MAX) { clip_y = 0; }

  /* render the image at 0, 0 */
  imlib_render_image_part_on_drawable_at_size(
      clip_x, clip_y, clip_x2 - clip_x, clip_y2 - clip_y, x + clip_x,
      y + clip_y, clip_x2 - clip_x, clip_y2 - clip_y);
  /* don't need that temporary buffer image anymore */
  imlib_free_image();
}

#else /* _WIN32 */

void cimlib_render(int x, int y, int /*width*/, int /*height*/,
                   uint32_t flush_interval, bool draw_blended) {
  /* Unlike the X11/Imlib2 path, actual drawing happens per-frame in
   * cimlib_draw_windows(), called from display-windows.cc's
   * begin_draw_text() -- there's no persistent drawable to render onto
   * here the way X11 has window.drawable; this port's whole DIB is
   * recreated every begin_draw_text() call. This function just records
   * the origin ${image} coordinates are relative to (text_start, the
   * same origin used for text) and runs the periodic whole-cache flush,
   * mirroring the Linux path's imlib_set_cache_size(0)/imlib_set_cache_
   * size(size) trick. */
  g_render_origin_x = x;
  g_render_origin_y = y;
  g_draw_blended = draw_blended;

  time_t now = time(nullptr);
  if (flush_interval != 0u &&
      static_cast<unsigned int>(now) - flush_interval >
          g_cimlib_cache_flush_last) {
    g_image_cache.clear();
    g_cimlib_cache_flush_last = static_cast<unsigned int>(now);
    LOG_DEBUG("flushing imlib2-windows image cache ({})", now);
  }
}

void cimlib_draw_windows(HDC hdc, int win_w, int win_h, int window_left,
                         int window_top) {
  if (image_list_start == nullptr || !g_gdiplus_started) { return; }

  Gdiplus::Graphics graphics(hdc);
  graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
  graphics.SetCompositingMode(g_draw_blended
                                  ? Gdiplus::CompositingModeSourceOver
                                  : Gdiplus::CompositingModeSourceCopy);
  /* Clip to the DIB bounds -- an ${image} positioned or sized past the
   * window edge (a common theme mistake) would otherwise be silently
   * dropped by GDI+ rather than clipped like Imlib2 does. */
  graphics.SetClip(Gdiplus::Rect(0, 0, win_w, win_h));

  time_t now = time(nullptr);
  for (struct image_list_s *cur = image_list_start; cur != nullptr;
       cur = cur->next) {
    bool force_reload =
        (cur->no_cache != 0) ||
        (cur->flush_interval != 0 &&
         static_cast<int>(now) % cur->flush_interval == 0);

    Gdiplus::Bitmap *bitmap = get_cached_image(cur->name, force_reload);
    if (bitmap == nullptr) { continue; }

    if (cur->wh_set == 0) {
      cur->w = dpi_scale(static_cast<int>(bitmap->GetWidth()));
      cur->h = dpi_scale(static_cast<int>(bitmap->GetHeight()));
    }

    int screen_x = g_render_origin_x + cur->x;
    int screen_y = g_render_origin_y + cur->y;
    int draw_x = screen_x - window_left;
    int draw_y = screen_y - window_top;

    graphics.DrawImage(bitmap, draw_x, draw_y, cur->w, cur->h);
  }
}

#endif /* _WIN32 */

void print_image_callback(struct text_object *obj, char *, unsigned int) {
  cimlib_add_image(obj->data.s);
}
