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

#include "cairo_dynamic.hh"

namespace conky {
namespace cairo_dyn {

namespace {

struct CairoApi {
  using ImageSurfaceCreate_fn = cairo_surface_t *(*)(cairo_format_t, int,
                                                      int);
  using SurfaceReference_fn = cairo_surface_t *(*)(cairo_surface_t *);
  using SurfaceDestroy_fn = void (*)(cairo_surface_t *);
  using SurfaceFlush_fn = void (*)(cairo_surface_t *);
  using ImageSurfaceGetData_fn = unsigned char *(*)(cairo_surface_t *);
  using ImageSurfaceGetStride_fn = int (*)(cairo_surface_t *);
  using SurfaceMarkDirty_fn = void (*)(cairo_surface_t *);
  using SurfaceGetDeviceScale_fn = void (*)(cairo_surface_t *, double *,
                                            double *);

  ImageSurfaceCreate_fn ImageSurfaceCreate = nullptr;
  SurfaceReference_fn SurfaceReference = nullptr;
  SurfaceDestroy_fn SurfaceDestroy = nullptr;
  SurfaceFlush_fn SurfaceFlush = nullptr;
  ImageSurfaceGetData_fn ImageSurfaceGetData = nullptr;
  ImageSurfaceGetStride_fn ImageSurfaceGetStride = nullptr;
  SurfaceMarkDirty_fn SurfaceMarkDirty = nullptr;
  SurfaceGetDeviceScale_fn SurfaceGetDeviceScale = nullptr;

  bool loaded = false;
  bool load_attempted = false;
};

CairoApi g_cairo;

template <typename Fn>
void resolve(HMODULE handle, const char *name, Fn *out) {
  *out = reinterpret_cast<Fn>(
      reinterpret_cast<void *>(GetProcAddress(handle, name)));
}

// Loads cairo.dll and resolves every symbol this file needs. Safe to call
// repeatedly -- only the first call does any work.
bool load_cairo_api() {
  if (g_cairo.load_attempted) return g_cairo.loaded;
  g_cairo.load_attempted = true;

  HMODULE handle = LoadLibraryA("cairo.dll");
  if (handle == nullptr) return false;

  resolve(handle, "cairo_image_surface_create", &g_cairo.ImageSurfaceCreate);
  resolve(handle, "cairo_surface_reference", &g_cairo.SurfaceReference);
  resolve(handle, "cairo_surface_destroy", &g_cairo.SurfaceDestroy);
  resolve(handle, "cairo_surface_flush", &g_cairo.SurfaceFlush);
  resolve(handle, "cairo_image_surface_get_data",
          &g_cairo.ImageSurfaceGetData);
  resolve(handle, "cairo_image_surface_get_stride",
          &g_cairo.ImageSurfaceGetStride);
  resolve(handle, "cairo_surface_mark_dirty", &g_cairo.SurfaceMarkDirty);
  resolve(handle, "cairo_surface_get_device_scale",
          &g_cairo.SurfaceGetDeviceScale);

  g_cairo.loaded = g_cairo.ImageSurfaceCreate != nullptr &&
                   g_cairo.SurfaceReference != nullptr &&
                   g_cairo.SurfaceDestroy != nullptr &&
                   g_cairo.SurfaceFlush != nullptr &&
                   g_cairo.ImageSurfaceGetData != nullptr &&
                   g_cairo.ImageSurfaceGetStride != nullptr &&
                   g_cairo.SurfaceMarkDirty != nullptr &&
                   g_cairo.SurfaceGetDeviceScale != nullptr;
  return g_cairo.loaded;
}

}  // namespace

bool available() { return load_cairo_api(); }

cairo_surface_t *surface_reference(cairo_surface_t *surface) {
  if (!load_cairo_api() || surface == nullptr) return surface;
  return g_cairo.SurfaceReference(surface);
}

cairo_surface_t *image_surface_create(int width, int height) {
  if (!load_cairo_api() || width <= 0 || height <= 0) return nullptr;
  return g_cairo.ImageSurfaceCreate(CAIRO_FORMAT_ARGB32, width, height);
}

void surface_destroy(cairo_surface_t *surface) {
  if (!load_cairo_api() || surface == nullptr) return;
  g_cairo.SurfaceDestroy(surface);
}

void surface_flush(cairo_surface_t *surface) {
  if (!load_cairo_api() || surface == nullptr) return;
  g_cairo.SurfaceFlush(surface);
}

unsigned char *image_surface_get_data(cairo_surface_t *surface) {
  if (!load_cairo_api() || surface == nullptr) return nullptr;
  return g_cairo.ImageSurfaceGetData(surface);
}

int image_surface_get_stride(cairo_surface_t *surface) {
  if (!load_cairo_api() || surface == nullptr) return 0;
  return g_cairo.ImageSurfaceGetStride(surface);
}

void surface_mark_dirty(cairo_surface_t *surface) {
  if (!load_cairo_api() || surface == nullptr) return;
  g_cairo.SurfaceMarkDirty(surface);
}

void surface_get_device_scale(cairo_surface_t *surface, double *x,
                              double *y) {
  if (!load_cairo_api() || surface == nullptr) {
    *x = 1.0;
    *y = 1.0;
    return;
  }
  g_cairo.SurfaceGetDeviceScale(surface, x, y);
}

}  // namespace cairo_dyn
}  // namespace conky
