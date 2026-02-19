cmake_minimum_required(VERSION 3.16)
project(pyxc_chapter04 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_program(LLVM_CONFIG NAMES llvm-config)
if(NOT LLVM_CONFIG)
  message(FATAL_ERROR "llvm-config not found in PATH. Install LLVM and add llvm-config to PATH.")
endif()

execute_process(
  COMMAND "${LLVM_CONFIG}" --cxxflags
  OUTPUT_VARIABLE LLVM_CXXFLAGS
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
  COMMAND "${LLVM_CONFIG}" --ldflags --libs support
  OUTPUT_VARIABLE LLVM_LDFLAGS
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

string(REGEX REPLACE "[\r\n]+" " " LLVM_CXXFLAGS "${LLVM_CXXFLAGS}")
string(REGEX REPLACE "[\r\n]+" " " LLVM_LDFLAGS "${LLVM_LDFLAGS}")

separate_arguments(LLVM_CXXFLAGS_LIST NATIVE_COMMAND "${LLVM_CXXFLAGS}")
separate_arguments(LLVM_LDFLAGS_LIST NATIVE_COMMAND "${LLVM_LDFLAGS}")

add_executable(pyxc pyxc.cpp)
target_compile_options(pyxc PRIVATE ${LLVM_CXXFLAGS_LIST})
target_link_options(pyxc PRIVATE ${LLVM_LDFLAGS_LIST})

if(NOT MSVC)
  target_compile_options(pyxc PRIVATE -Wall -Wextra -pedantic)
endif()
