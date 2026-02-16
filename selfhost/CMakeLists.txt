cmake_minimum_required(VERSION 3.16)
project(pyxc_selfhost LANGUAGES C CXX)

# C11 standard for utility files
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# C++17 standard for LLVM bridge
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

#===----------------------------------------------------------------------===//
# LLVM Configuration
#===----------------------------------------------------------------------===//

# Optional LLVM install prefix (expects llvm-config at <prefix>/bin/llvm-config)
set(LLVM_PREFIX "" CACHE PATH "Path to LLVM install prefix")
set(LLVM_CONFIG "" CACHE FILEPATH "Path to llvm-config")

# If a stale cached path is set, clear it so find_program can recover.
if(LLVM_CONFIG AND NOT EXISTS "${LLVM_CONFIG}")
  message(WARNING "Configured LLVM_CONFIG does not exist: ${LLVM_CONFIG}. Falling back to search.")
  set(LLVM_CONFIG "" CACHE FILEPATH "Path to llvm-config" FORCE)
endif()

# Find llvm-config from explicit prefix hints or PATH.
if(NOT LLVM_CONFIG)
  unset(LLVM_CONFIG CACHE)
  set(_LLVM_CONFIG_HINTS "")
  if(LLVM_PREFIX)
    list(APPEND _LLVM_CONFIG_HINTS "${LLVM_PREFIX}/bin")
  endif()
  if(APPLE)
    list(APPEND _LLVM_CONFIG_HINTS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin)
  endif()
  find_program(LLVM_CONFIG
    NAMES llvm-config
    HINTS ${_LLVM_CONFIG_HINTS}
  )
endif()

# Error if llvm-config not found
if(NOT LLVM_CONFIG OR LLVM_CONFIG STREQUAL "LLVM_CONFIG-NOTFOUND")
  message(FATAL_ERROR "llvm-config not found. Set LLVM_CONFIG or LLVM_PREFIX.\n"
                      "Example: cmake -DLLVM_PREFIX=/path/to/llvm ..")
endif()

message(STATUS "Using llvm-config: ${LLVM_CONFIG}")

# Get LLVM components we need
set(LLVM_COMPONENTS core support native nativecodegen mc target targetparser)

# Validate llvm-config and query flags/libraries.
execute_process(
  COMMAND "${LLVM_CONFIG}" --version
  RESULT_VARIABLE LLVM_VERSION_RESULT
  OUTPUT_VARIABLE LLVM_VERSION
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT LLVM_VERSION_RESULT EQUAL 0)
  message(FATAL_ERROR "Failed to execute llvm-config at ${LLVM_CONFIG}")
endif()

execute_process(
  COMMAND "${LLVM_CONFIG}" --cxxflags
  RESULT_VARIABLE LLVM_CXXFLAGS_RESULT
  OUTPUT_VARIABLE LLVM_CXXFLAGS_RAW
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT LLVM_CXXFLAGS_RESULT EQUAL 0)
  message(FATAL_ERROR "Failed to query LLVM cxxflags from ${LLVM_CONFIG}")
endif()
separate_arguments(LLVM_CXXFLAGS NATIVE_COMMAND "${LLVM_CXXFLAGS_RAW}")

execute_process(
  COMMAND "${LLVM_CONFIG}" --ldflags
  RESULT_VARIABLE LLVM_LDFLAGS_RESULT
  OUTPUT_VARIABLE LLVM_LDFLAGS_RAW
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT LLVM_LDFLAGS_RESULT EQUAL 0)
  message(FATAL_ERROR "Failed to query LLVM ldflags from ${LLVM_CONFIG}")
endif()
separate_arguments(LLVM_LDFLAGS NATIVE_COMMAND "${LLVM_LDFLAGS_RAW}")

execute_process(
  COMMAND "${LLVM_CONFIG}" --libs ${LLVM_COMPONENTS}
  RESULT_VARIABLE LLVM_LIBS_RESULT
  OUTPUT_VARIABLE LLVM_LIBS_RAW
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT LLVM_LIBS_RESULT EQUAL 0)
  message(FATAL_ERROR "Failed to query LLVM libs from ${LLVM_CONFIG}")
endif()
separate_arguments(LLVM_LIBS NATIVE_COMMAND "${LLVM_LIBS_RAW}")

execute_process(
  COMMAND "${LLVM_CONFIG}" --system-libs
  RESULT_VARIABLE LLVM_SYSTEM_LIBS_RESULT
  OUTPUT_VARIABLE LLVM_SYSTEM_LIBS_RAW
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT LLVM_SYSTEM_LIBS_RESULT EQUAL 0)
  message(FATAL_ERROR "Failed to query LLVM system libs from ${LLVM_CONFIG}")
endif()
separate_arguments(LLVM_SYSTEM_LIBS NATIVE_COMMAND "${LLVM_SYSTEM_LIBS_RAW}")

#===----------------------------------------------------------------------===//
# Mac-specific library handling (zstd, etc.)
#===----------------------------------------------------------------------===//

set(EXTRA_LIBDIR "" CACHE PATH "Optional extra linker search path")
set(ZSTD_LIBDIR "" CACHE PATH "Optional zstd library directory")
set(EXTRA_LINK_DIRS "")

if(EXTRA_LIBDIR)
  list(APPEND EXTRA_LINK_DIRS "${EXTRA_LIBDIR}")
endif()

# Auto-detect Homebrew's zstd on Mac if not explicitly set
if(NOT ZSTD_LIBDIR AND APPLE)
  find_program(BREW_EXECUTABLE brew)
  if(BREW_EXECUTABLE)
    execute_process(
      COMMAND "${BREW_EXECUTABLE}" --prefix zstd
      OUTPUT_VARIABLE BREW_ZSTD_PREFIX
      RESULT_VARIABLE BREW_ZSTD_RESULT
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(BREW_ZSTD_RESULT EQUAL 0 AND EXISTS "${BREW_ZSTD_PREFIX}/lib")
      set(ZSTD_LIBDIR "${BREW_ZSTD_PREFIX}/lib")
      message(STATUS "Found Homebrew zstd at: ${ZSTD_LIBDIR}")
    endif()
  endif()
endif()

if(ZSTD_LIBDIR)
  list(APPEND EXTRA_LINK_DIRS "${ZSTD_LIBDIR}")
endif()

#===----------------------------------------------------------------------===//
# Build Targets
#===----------------------------------------------------------------------===//

# LLVM Bridge library (compiled as C++)
add_library(llvm_bridge STATIC llvm_bridge.cpp)
target_include_directories(llvm_bridge PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(llvm_bridge PRIVATE -g -O0 ${LLVM_CXXFLAGS})

# String utilities library (C)
add_library(string_utils STATIC string_utils.c)
target_include_directories(string_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# File utilities library (C)
add_library(file_utils STATIC file_utils.c)
target_include_directories(file_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Test program
add_executable(test_bridge test_bridge.c)
target_link_libraries(test_bridge PRIVATE
  llvm_bridge
  string_utils
  file_utils
  ${LLVM_LIBS}
  ${LLVM_SYSTEM_LIBS}
)
target_link_options(test_bridge PRIVATE ${LLVM_LDFLAGS})

if(EXTRA_LINK_DIRS)
  target_link_directories(test_bridge PRIVATE ${EXTRA_LINK_DIRS})
endif()

#===----------------------------------------------------------------------===//
# Installation (optional)
#===----------------------------------------------------------------------===//

# Install libraries for use in later phases
install(TARGETS llvm_bridge string_utils file_utils
        ARCHIVE DESTINATION lib)
install(FILES llvm_bridge.h DESTINATION include)

#===----------------------------------------------------------------------===//
# Testing
#===----------------------------------------------------------------------===//

enable_testing()

# Add test that runs test_bridge
add_test(NAME bridge_test COMMAND test_bridge)

# Custom target to run tests with verbose output
add_custom_target(check
  COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
  DEPENDS test_bridge
  COMMENT "Running tests..."
)

#===----------------------------------------------------------------------===//
# Status output
#===----------------------------------------------------------------------===//

message(STATUS "")
message(STATUS "===== Pyxc Self-Hosting Configuration =====")
message(STATUS "LLVM Config: ${LLVM_CONFIG}")
message(STATUS "LLVM Version: ${LLVM_VERSION}")
message(STATUS "C Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "C++ Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
if(ZSTD_LIBDIR)
  message(STATUS "zstd Library: ${ZSTD_LIBDIR}")
endif()
message(STATUS "==========================================")
message(STATUS "")
message(STATUS "Build commands:")
message(STATUS "  cmake -B build")
message(STATUS "  cmake --build build")
message(STATUS "  ./build/test_bridge")
message(STATUS "")
