cmake_minimum_required (VERSION 3.22)

project(apb
  LANGUAGES CXX
  VERSION 0.0.0
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)  # for argparse
# user paths to htslib if needed
set(HTSLIB_INCLUDE_DIR "" CACHE PATH "Path to directory that contains htslib/hts.h")
set(HTSLIB_LIBRARY "" CACHE FILEPATH "Absolute path to libhts.{so,dylib,a}")

option(MAKE_TEST "Build the unit test binary" OFF)

find_package(PkgConfig REQUIRED)
# >=3.38: reads.tags relies on the built-in json_valid() (JSON support was
# folded into core sqlite3 in 3.38; before that it required the JSON1
# extension to be compiled in separately).
pkg_check_modules(DEPS REQUIRED IMPORTED_TARGET sqlite3>=3.38)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(
  termbox2
  GIT_REPOSITORY https://github.com/termbox/termbox2.git
  GIT_TAG        605398fa79108412976191e062ea14bd4bd30213
  GIT_PROGRESS   TRUE
  GIT_SHALLOW    TRUE
  SYSTEM
)
FetchContent_MakeAvailable(termbox2)
FetchContent_Declare(
    plog
    GIT_REPOSITORY https://github.com/SergiusTheBest/plog
    GIT_TAG        1.1.10
    GIT_SHALLOW    true
    SYSTEM
)
FetchContent_MakeAvailable(plog)
FetchContent_Declare(
    argparse
    GIT_REPOSITORY https://github.com/p-ranav/argparse.git
    GIT_TAG v3.2
    GIT_SHALLOW TRUE
    SYSTEM
)
FetchContent_MakeAvailable(argparse)
FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG        12.2.0
    GIT_SHALLOW    TRUE
    SYSTEM
)
FetchContent_MakeAvailable(fmt)

if(MAKE_TEST)
  FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.8.1
    GIT_SHALLOW    TRUE
    SYSTEM
  )
  FetchContent_MakeAvailable(Catch2)
endif()

add_library(termbox2 INTERFACE)
target_include_directories(termbox2 SYSTEM INTERFACE "${termbox2_SOURCE_DIR}")


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
  src/backend/PileupDB.cpp
  src/backend/pileup_ingest.cpp
  src/demo/demo.cpp
  src/app/main.cpp
  src/app/cli.cpp
  src/app/subcommands.cpp
  src/app/tui.cpp
  src/app/widgets.cpp
  src/frontend/extb/extb.cpp
  src/frontend/extb/widgets/box.cpp
  src/frontend/tb2_impl.cpp
  src/frontend/input.cpp
  src/frontend/history.cpp
  src/app/cmd.cpp
  src/backend/hts_types.cpp
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
)
target_include_directories(apb
  PRIVATE
  src
)
target_link_libraries(apb
  PRIVATE
  termbox2
  plog::plog
  argparse
  fmt::fmt-header-only
  ${HTSLIB_TARGET}
  PkgConfig::DEPS
)

if(MAKE_TEST)
  enable_testing()
  add_executable(apb-tests
    src/backend/PileupDB.cpp
    src/backend/pileup_ingest.cpp
    src/backend/hts_types.cpp
    tests/test_stringify.cpp
    tests/test_schema.cpp
    tests/test_ingest.cpp
    tests/test_query.cpp
  )
  target_include_directories(apb-tests PRIVATE src)
  target_link_libraries(apb-tests
    PRIVATE
    Catch2::Catch2WithMain
    fmt::fmt-header-only
    ${HTSLIB_TARGET}
    PkgConfig::DEPS
    plog::plog
  )
  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
  include(Catch)
  catch_discover_tests(apb-tests)
endif()
