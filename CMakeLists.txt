cmake_minimum_required (VERSION 3.22)

project(apb
  LANGUAGES CXX
  VERSION 0.1.0
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# user paths to htslib if needed
set(HTSLIB_INCLUDE_DIR "" CACHE PATH "Path to directory that contains htslib/hts.h")
set(HTSLIB_LIBRARY "" CACHE FILEPATH "Absolute path to libhts.{so,dylib,a}")

find_package(PkgConfig REQUIRED)
# >=3.38: reads.tags relies on the built-in json_valid() (JSON support was
# folded into core sqlite3 in 3.38; before that it required the JSON1
# extension to be compiled in separately).
pkg_check_modules(DEPS REQUIRED IMPORTED_TARGET sqlite3>=3.38)
include(CheckSymbolExists)
set(CMAKE_REQUIRED_INCLUDES ${DEPS_INCLUDE_DIRS})
set(CMAKE_REQUIRED_LIBRARIES PkgConfig::DEPS)
check_symbol_exists(sqlite3_hard_heap_limit64 "sqlite3.h" HAVE_HEAP_LIM)

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

set(APB_COMPILE_DEFINITIONS
  APB_VERSION="${PROJECT_VERSION}"
  TB_OPT_EGC
)

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
target_compile_definitions(apb PRIVATE ${APB_COMPILE_DEFINITIONS})
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

# Test files are co-located with the module they test
file(GLOB_RECURSE APB_TEST_SOURCES CONFIGURE_DEPENDS "src/*_test.cpp")

add_executable(apb-tests
  tests/main.cpp
  src/backend/hts_sql.cpp
  src/backend/hts_types.cpp
  src/frontend/tb2_impl.cpp
  ${APB_TEST_SOURCES}
)
target_compile_definitions(apb-tests PRIVATE ${APB_COMPILE_DEFINITIONS})
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

# tests run as part of build (and must pass).
add_custom_command(
  TARGET apb-tests
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E echo "---- running apb-tests ----"
  COMMAND apb-tests
  COMMENT "Running apb-tests"
  VERBATIM
)
