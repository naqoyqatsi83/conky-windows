include(CTest)

include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(${CMAKE_BINARY_DIR})
include_directories(${conky_includes})

# Windows compat layer — mirrors src/CMakeLists.txt. include_directories()
# there is scoped to src/ and its descendants, so it never reaches this
# sibling directory; without it, conky.h's `#include <sys/utsname.h>` (a
# POSIX header MinGW doesn't provide) fails to find the compat shim at
# src/compat/sys/utsname.h and every test-*.cc TU fails to compile.
if(OS_WINDOWS)
  include_directories(${CMAKE_SOURCE_DIR}/src/compat)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -include${CMAKE_SOURCE_DIR}/src/compat/compat_windows.h")
  add_definitions(-D_WIN32_WINNT=0x0601)
endif()

file(GLOB test_srcs test-*.cc)

if(NOT OS_LINUX)
  list(FILTER test_srcs EXCLUDE REGEX ".*linux.*\.cc?")
endif()

if(NOT OS_DARWIN)
  list(FILTER test_srcs EXCLUDE REGEX ".*darwin.*\.cc?")
endif()

if(NOT OS_WINDOWS)
  list(FILTER test_srcs EXCLUDE REGEX ".*windows.*\.cc?")
endif()

add_library(Catch2 STATIC catch2/catch_amalgamated.cpp)

add_executable(test-conky test-common.cc ${test_srcs})
target_include_directories(test-conky PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR})
target_link_libraries(test-conky
  PRIVATE Catch2
  PUBLIC conky_core
)
if(OS_WINDOWS)
  # catch_discover_tests() runs the freshly-linked test binary *during the
  # build itself* to enumerate individual TEST_CASEs for ctest. On this
  # MinGW toolchain that binary isn't necessarily runnable yet at that
  # point — it can depend on MinGW runtime DLLs (libstdc++, liblua, etc.)
  # that only get bundled next to conky.exe in a separate post-build CI
  # step, never for tests/test-conky.exe. Confirmed failing in CI with
  # STATUS_DLL_NOT_FOUND (0xc0000135) even though the binary links fine.
  # A plain add_test() just registers the command for `ctest`/manual
  # invocation later, without ever executing it at configure/build time,
  # sidestepping the whole DLL-availability-at-build-time question. The
  # cost is coarser ctest granularity (one aggregate entry instead of one
  # per Catch2 TEST_CASE) — acceptable; run test-conky.exe directly for
  # per-case output.
  add_test(NAME test-conky COMMAND test-conky)
else()
  catch_discover_tests(test-conky)
endif()

if(RUN_TESTS)
  add_custom_command(TARGET test-conky
    POST_BUILD
    COMMAND ctest -C $<CONFIGURATION> --output-on-failure)
endif()

if(CODE_COVERAGE)
  set(COVERAGE_LCOV_EXCLUDES
    "*/include/c++/v1/*"
    "/usr/include/*"
    "/usr/lib/*"
    "/usr/local/Cellar/*"
    "/usr/local/include/*"
    "/usr/local/lib/*"
    "/usr/include/libkern/i386/*"
    "/usr/include/sys/_types/*"
    "/usr/local/opt/gettext/include/*"
    "${CMAKE_CURRENT_SOURCE_DIR}/*"
    "${CMAKE_CURRENT_SOURCE_DIR}/catch2/*"
    "${CMAKE_SOURCE_DIR}/3rdparty/*")

  setup_target_for_coverage_lcov_html(NAME
    test-conky-coverage-html
    EXECUTABLE
    test-conky
    DEPENDENCIES
    test-conky)
  setup_target_for_coverage_lcov_txt(NAME
    test-conky-coverage-txt
    EXECUTABLE
    test-conky
    DEPENDENCIES
    test-conky)
endif()
