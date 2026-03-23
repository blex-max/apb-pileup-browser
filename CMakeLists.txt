cmake_minimum_required (VERSION 3.22)

project(t2-proto
  LANGUAGES CXX
  VERSION 0.0.0
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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

add_executable(t2-proto
  src/main.cpp
  src/extb.cpp
  src/app.cpp
  src/cmd.cpp
  src/input.cpp
  src/tb2_impl.cpp
)
target_include_directories(t2-proto
  PRIVATE src
)
target_link_libraries(t2-proto
  PRIVATE
  termbox2
  plog::plog
  argparse
)
