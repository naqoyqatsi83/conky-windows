# curl, vendored for Windows only.
#
# Prebuilt from https://curl.se/windows/ (official curl.se MinGW-w64
# builds), release curl-8.22.0_1-win64-mingw.zip. curl itself is MIT-like
# (see licenses/COPYING-curl.txt); the DLL statically links several other
# libraries (LibreSSL, brotli, libssh2, nghttp2/nghttp3/ngtcp2, libpsl,
# zlib-ng, zstd) whose licenses are also under licenses/ for attribution.
#
# No pkg-config is available on this MinGW toolchain, so unlike Linux/BSD
# (which discover libcurl via pkg_check_modules/find_package(CURL) in
# ConkyPlatformChecks.cmake), Windows gets its own vendored copy here,
# mirroring how 3rdparty/cairo is vendored (see conky-windows issue #10).
#
# Unlike Cairo, this DLL is a hard link-time dependency (matching how
# upstream conky treats libcurl on every other platform) rather than
# resolved dynamically at runtime -- ${curl}/${rss}/${stock}/
# ${github_notifications} are all compile-time opt-ins already gated
# behind BUILD_CURL/BUILD_RSS (see conky-windows issue #18), so there's
# no "keep starting without it" requirement the way Cairo needed for
# always-compiled-in Lua draw hook support.
#
# TLS backend is LibreSSL, statically linked into the DLL (no separate
# libssl/libcrypto DLL to ship) using Windows' own certificate store for
# trust roots (the "NativeCA" feature, via crypt32.dll) rather than a
# bundled ca-bundle.crt -- confirmed via `curl.exe --version` on the
# vendored build before stripping bin/ down to just the DLL + import lib.

set(CURL_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(CURL_DLL "${CMAKE_CURRENT_SOURCE_DIR}/lib/libcurl-x64.dll")
set(CURL_IMPORT_LIB "${CMAKE_CURRENT_SOURCE_DIR}/lib/libcurl.dll.a")

add_library(curl_windows INTERFACE)
target_include_directories(curl_windows INTERFACE "${CURL_INCLUDE_DIR}")
target_link_libraries(curl_windows INTERFACE "${CURL_IMPORT_LIB}")

# Exposed for the top-level build to stage libcurl-x64.dll next to build
# output (same pattern as CAIRO_DLL in 3rdparty/cairo/CMakeLists.txt).
set(CURL_DLL "${CURL_DLL}" CACHE INTERNAL "Path to vendored libcurl-x64.dll")
