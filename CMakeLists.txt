cmake_minimum_required (VERSION 3.22)

project(apb
  LANGUAGES CXX
  VERSION 0.0.1
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# user paths to htslib if needed
set(HTSLIB_INCLUDE_DIR "" CACHE PATH "Path to directory that contains htslib/hts.h")
set(HTSLIB_LIBRARY "" CACHE FILEPATH "Absolute path to libhts.{so,dylib,a}")

option(MAKE_TEST "Build apb-tests and run it as part of the build (opt out with -DMAKE_TEST=OFF)" ON)

find_package(PkgConfig REQUIRED)
# >=3.38: reads.tags relies on the built-in json_valid() (JSON support was
# folded into core sqlite3 in 3.38; before that it required the JSON1
# extension to be compiled in separately).
pkg_check_modules(DEPS REQUIRED IMPORTED_TARGET sqlite3>=3.38)

add_library(doctest INTERFACE)
target_include_directories(doctest SYSTEM INTERFACE "${CMAKE_SOURCE_DIR}/vendor/doctest")

add_library(termbox2 INTERFACE)
target_include_directories(termbox2 SYSTEM INTERFACE "${CMAKE_SOURCE_DIR}/vendor/termbox2")

add_library(plog INTERFACE)
add_library(plog::plog ALIAS plog)
target_include_directories(plog SYSTEM INTERFACE "${CMAKE_SOURCE_DIR}/vendor/plog/include")

add_library(fmt-header-only INTERFACE)
add_library(fmt::fmt-header-only ALIAS fmt-header-only)
target_include_directories(fmt-header-only SYSTEM INTERFACE "${CMAKE_SOURCE_DIR}/vendor/fmt/include")
target_compile_definitions(fmt-header-only INTERFACE FMT_HEADER_ONLY=1)

set(HTSLIB_TARGET "")
if(HTSLIB_INCLUDE_DIR AND HTSLIB_LIBRARY)
  message(STATUS "Using user-specified htslib install. Must be >=1.17, but this is not enforced by the installer.")
  add_library(htslib UNKNOWN IMPORTED GLOBAL)
  add_library(htslib::htslib ALIAS htslib)
  set_target_properties(htslib PROPERTIES
    IMPORTED_LOCATION "${HTSLIB_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${HTSLIB_INCLUDE_DIR}"
  )
  set(HTSLIB_TARGET htslib::htslib)
endif()

if(NOT HTSLIB_TARGET)
  message(STATUS "Looking for htslib via PkgConfig")
  # assuming the pkg-config module is called "htslib"
  pkg_check_modules(HTSLIB QUIET IMPORTED_TARGET htslib>=1.17)
  if(HTSLIB_FOUND)
    message(STATUS "Located htslib>=1.17 install via pkg-config.")
    set(HTSLIB_TARGET PkgConfig::HTSLIB)
  endif()
endif()

if(NOT HTSLIB_TARGET)
  message(FATAL_ERROR
    "Could not find htslib directly or via pkg-config. "
    "Set both HTSLIB_INCLUDE_DIR and HTSLIB_LIBRARY to use user install."
  )
endif()

add_executable(apb
  src/app/main.cpp
  src/app/widgets.cpp
  src/app/cmd.cpp
  src/app/manual.cpp
  src/app/event.cpp
  src/backend/hts_sql.cpp
  src/backend/hts_types.cpp
  src/frontend/tb2_impl.cpp
  src/frontend/input.cpp
  src/frontend/history.cpp
  src/demo/demo.cpp
)
target_compile_definitions(apb PRIVATE
  APB_VERSION="${PROJECT_VERSION}"
  TB_OPT_EGC
)
target_compile_options(apb
  PRIVATE
  -Wall
  -Wextra
  -Wpedantic
  -Wshadow
  -Wconversion
  -Wsign-conversion
  -Wfloat-equal
  -Wnull-dereference
  -Wformat=2
  -Werror=return-type
  -Wno-c99-designator
)
target_include_directories(apb
  PRIVATE
  src
)
target_link_libraries(apb
  PRIVATE
  termbox2
  plog::plog
  fmt::fmt-header-only
  ${HTSLIB_TARGET}
  PkgConfig::DEPS
)

if(MAKE_TEST)
  # Test files are co-located with the module they cover (e.g.
  # src/backend/hts_sql_test.cpp next to hts_sql.cpp), rather than kept in
  # a separate tests/ tree, so a glob (re-evaluated at build time via
  # CONFIGURE_DEPENDS) is the one place in this project a source list
  # isn't hand-maintained -- an explicit list would silently drop a new
  # *_test.cpp that nobody remembered to add here, defeating the point.
  file(GLOB_RECURSE APB_TEST_SOURCES CONFIGURE_DEPENDS "src/*_test.cpp")

  add_executable(apb-tests
    tests/main.cpp
    src/backend/hts_sql.cpp
    src/backend/hts_types.cpp
    src/frontend/tb2_impl.cpp
    ${APB_TEST_SOURCES}
  )
  target_compile_definitions(apb-tests PRIVATE
    APB_VERSION="${PROJECT_VERSION}"
    TB_OPT_EGC
  )
  target_include_directories(apb-tests PRIVATE src)
  target_link_libraries(apb-tests
    PRIVATE
    doctest
    termbox2
    fmt::fmt-header-only
    ${HTSLIB_TARGET}
    PkgConfig::DEPS
    plog::plog
  )

  # Exercised as part of the build itself (opt out with -DMAKE_TEST=OFF)
  # rather than relying on a separate `ctest`/CI invocation -- a failing
  # assertion fails the build. The echo is a real command (not just the
  # COMMENT, which some generators/parallel builds swallow or bury) so
  # the test run is unmistakable in the build log.
  add_custom_command(
    TARGET apb-tests
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "---- running apb-tests ----"
    COMMAND apb-tests
    COMMENT "Running apb-tests"
    VERBATIM
  )
endif()
