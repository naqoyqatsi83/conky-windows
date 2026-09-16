# libxml2, vendored for Windows only.
#
# Prebuilt from https://repo.msys2.org/mingw/mingw64/ (MSYS2's official
# mingw-w64 package repo, actively maintained -- unlike the commonly-cited
# zlatkovic.com Windows libxml2 build, which is from 2015 and unmaintained).
# mingw-w64-x86_64-libxml2 2.15.4-1 (MIT license, licenses/COPYING-libxml2),
# built with MSYS2's own MinGW-w64 GCC. ABI-compatible with this project's
# TDM-GCC-64 toolchain: both target the same x86_64-w64-mingw32 triple and
# Windows x64 calling convention, the same way the vendored curl build in
# 3rdparty/curl (a different but ABI-compatible toolchain build) already
# works. See conky-windows issue #30.
#
# Unlike curl (a single self-contained DLL with everything statically
# linked), libxml2-16.dll has two real runtime dependencies of its own:
# libiconv-2.dll (mingw-w64-x86_64-libiconv, LGPL, licenses/
# COPYING.LIB-libiconv) and zlib1.dll (mingw-w64-x86_64-zlib, zlib
# license, licenses/LICENSE-zlib). Both vendored alongside it for the
# same reason curl's DLL is vendored rather than assumed present.
# libcharset-1.dll is bundled too (ships alongside libiconv upstream) even
# though it isn't a direct PE import of any of the above, in case iconv
# ever resolves it via LoadLibrary at runtime.
#
# No pkg-config on this MinGW toolchain, so unlike Linux/BSD (which
# discover libxml2 via pkg_check_modules/find_package(LibXml2) in
# ConkyPlatformChecks.cmake), Windows gets its own vendored copy here,
# mirroring 3rdparty/curl. Like curl, this is a normal hard link-time
# dependency (added directly to conky_libs/conky_includes) rather than
# resolved dynamically at runtime -- ${rss} is a compile-time opt-in
# already gated behind BUILD_RSS, so there's no "keep starting without
# it" requirement.

set(LIBXML2_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(LIBXML2_DLL "${CMAKE_CURRENT_SOURCE_DIR}/lib/libxml2-16.dll")
set(LIBXML2_IMPORT_LIB "${CMAKE_CURRENT_SOURCE_DIR}/lib/libxml2.dll.a")

add_library(libxml2_windows INTERFACE)
target_include_directories(libxml2_windows INTERFACE "${LIBXML2_INCLUDE_DIR}")
target_link_libraries(libxml2_windows INTERFACE "${LIBXML2_IMPORT_LIB}")

# Exposed for the top-level build to stage libxml2-16.dll (and its own
# runtime deps) next to build output (same pattern as CURL_DLL in
# 3rdparty/curl/CMakeLists.txt).
set(LIBXML2_DLL "${LIBXML2_DLL}" CACHE INTERNAL "Path to vendored libxml2-16.dll")
set(LIBXML2_EXTRA_DLLS
  "${CMAKE_CURRENT_SOURCE_DIR}/lib/libiconv-2.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/lib/libcharset-1.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/lib/zlib1.dll"
  CACHE INTERNAL "Paths to libxml2-16.dll's own runtime dependencies")
