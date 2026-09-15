# Cairo, vendored for Windows only.
#
# Prebuilt from https://github.com/preshing/cairo-windows (Unlicense),
# release cairo-windows-1.17.2.zip. Cairo itself is dual LGPL-2.1/MPL-1.1
# (see COPYING-LGPL-2.1 / COPYING-MPL-1.1 in this directory).
#
# No pkg-config is available on this MinGW toolchain, so unlike Linux/BSD
# (which discover Cairo via pkg_check_modules in ConkyPlatformChecks.cmake),
# Windows gets its own vendored copy here. The import library
# (libcairo.dll.a) was regenerated for MinGW from the shipped DLL's own
# export table via `gendef cairo.dll && dlltool -d cairo.def -D cairo.dll -l
# libcairo.dll.a`, since the upstream release only ships an MSVC-format
# .lib.

set(CAIRO_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(CAIRO_DLL "${CMAKE_CURRENT_SOURCE_DIR}/lib/cairo.dll")
set(CAIRO_IMPORT_LIB "${CMAKE_CURRENT_SOURCE_DIR}/lib/libcairo.dll.a")

# NOTE: lua/CMakeLists.txt's conky-cairo module (the `require("cairo")` Lua
# module, a separate opt-in DLL) links against ${CAIRO_IMPORT_LIB} directly
# via the luacairo_libs/luacairo_includes variables set in
# ConkyPlatformChecks.cmake's BUILD_LUA_CAIRO/OS_WINDOWS branch, rather than
# a CMake IMPORTED target here -- ConkyPlatformChecks.cmake runs (from the
# top-level CMakeLists.txt) before add_subdirectory(3rdparty), so a target
# defined in this file wouldn't exist yet at that point.

# Header-only variant: exposes Cairo's headers/types without linking the
# import library. conky.exe's own display_output_windows::drawing_surface()
# resolves the handful of Cairo symbols it needs dynamically at runtime
# (LoadLibraryA/GetProcAddress, mirroring src/data/hardware/nvidia_nvml.cc)
# instead of linking cairo_windows directly, so that conky.exe keeps
# starting fine even if cairo.dll is ever missing.
add_library(cairo_headers INTERFACE)
target_include_directories(cairo_headers INTERFACE "${CAIRO_INCLUDE_DIR}")

# Exposed for the top-level build to stage cairo.dll next to build output.
set(CAIRO_DLL "${CAIRO_DLL}" CACHE INTERNAL "Path to vendored cairo.dll")
