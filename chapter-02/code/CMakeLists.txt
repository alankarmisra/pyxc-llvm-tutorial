# Require a reasonably modern CMake version.
cmake_minimum_required(VERSION 3.16)

# Name the project and say this project builds C++ code.
project(pyxc_chapter02 LANGUAGES CXX)

# Use C++17.
set(CMAKE_CXX_STANDARD 17)

# Require exactly the configured standard (don't silently fall back).
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Disable compiler-specific language extensions (use standard C++).
set(CMAKE_CXX_EXTENSIONS OFF)

# Build an executable named `pyxc` from `pyxc.cpp`.
add_executable(pyxc pyxc.cpp)

# Use strong warnings on non-MSVC compilers.
if (NOT MSVC)
  target_compile_options(pyxc PRIVATE -Wall -Wextra -pedantic)
endif()
