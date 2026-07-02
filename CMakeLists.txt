cmake_minimum_required (VERSION 3.22)

project(pileup-browser
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

include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(
  termbox2
  GIT_REPOSITORY https://github.com/termbox/termbox2.git
  GIT_TAG        605398fa79108412976191e062ea14bd4bd30213
  GIT_PROGRESS   TRUE
  GIT_SHALLOW    TRUE
  EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(termbox2)
FetchContent_Declare(
    plog
    GIT_REPOSITORY https://github.com/SergiusTheBest/plog
    GIT_TAG        1.1.10
    GIT_SHALLOW    true
)
FetchContent_MakeAvailable(plog)
FetchContent_Declare(
    argparse
    GIT_REPOSITORY https://github.com/p-ranav/argparse.git
    GIT_TAG v3.2
)
FetchContent_MakeAvailable(argparse)

add_library(termbox2 INTERFACE)
target_include_directories(termbox2 INTERFACE "${termbox2_SOURCE_DIR}")

set(HTSLIB_TARGET "")
set(_htslib_inc "")
set(_htslib_lib "")

if(HTSLIB_INCLUDE_DIR AND HTSLIB_LIBRARY)
  message(STATUS "Using user-specified htslib install. Must be >=1.14, but this is not enforced.")
  set(_htslib_inc "${HTSLIB_INCLUDE_DIR}")
  set(_htslib_lib "${HTSLIB_LIBRARY}")
endif()

if(_htslib_inc AND _htslib_lib)
  add_library(htslib UNKNOWN IMPORTED GLOBAL)
  add_library(htslib::htslib ALIAS htslib)
  set_target_properties(htslib PROPERTIES
    IMPORTED_LOCATION "${_htslib_lib}"
    INTERFACE_INCLUDE_DIRECTORIES "${_htslib_inc}"
  )
  set(HTSLIB_TARGET htslib::htslib)
endif()

message(STATUS "Finding htslib via PkgConfig")
find_package(PkgConfig)
if(NOT HTSLIB_TARGET AND PkgConfig_FOUND)
  # assuming the pkg-config module is called "htslib"
  pkg_check_modules(HTSLIB QUIET IMPORTED_TARGET htslib>=1.14)
  if(HTSLIB_FOUND)
    message(STATUS "Located htslib>=1.14 install via pkg-config.")
    set(HTSLIB_TARGET PkgConfig::HTSLIB)
  endif()
endif()

if(NOT HTSLIB_TARGET)
  message(FATAL_ERROR
    "Could not find htslib directly or via pkg-config. "
    "Set both HTSLIB_INCLUDE_DIR and HTSLIB_LIBRARY to use user install."
  )
endif()

add_executable(pileup-browser
  src/main.cpp
  src/extb/extb.cpp
  src/extb/extb-box.cpp
  src/app.cpp
  src/cmd.cpp
  src/PileupContext.cpp
  src/input.cpp
  src/tb2_impl.cpp
  src/table.cpp
  src/demo.cpp
  src/hts/accessors.cpp
)
target_compile_options(pileup-browser
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
target_include_directories(pileup-browser
  PRIVATE
  src
)
target_link_libraries(pileup-browser
  PRIVATE
  termbox2
  plog::plog
  argparse
  ${HTSLIB_TARGET}
)
