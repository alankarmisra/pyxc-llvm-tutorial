cmake_minimum_required(VERSION 3.16)
project(pyxc LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Find llvm-config
#
# Priority order:
#   1. LLVM_CONFIG set explicitly on the cmake command line
#   2. LLVM_PREFIX set explicitly (custom build) — looks in <prefix>/bin/
#   3. PATH — covers Homebrew, the official Windows installer, and system installs
#
# For Homebrew:   ensure $LLVM_ROOT/bin is on PATH (see chapter-04)
# For Windows:    ensure C:\Program Files\LLVM\bin is on PATH (installer option)
# For custom build: pass -DLLVM_PREFIX=$HOME/llvm-21-with-clang-lld-lldb
# ---------------------------------------------------------------------------

set(LLVM_PREFIX "" CACHE PATH "Optional: path to LLVM install prefix (custom build)")
set(LLVM_CONFIG "" CACHE FILEPATH "Optional: explicit path to llvm-config")

if(NOT LLVM_CONFIG)
  if(LLVM_PREFIX)
    # Custom build: look only inside the given prefix
    find_program(LLVM_CONFIG NAMES llvm-config
                 HINTS "${LLVM_PREFIX}/bin"
                 NO_DEFAULT_PATH)
  endif()
endif()

if(NOT LLVM_CONFIG)
  # Check PATH first (works if the user set it up per chapter-04)
  find_program(LLVM_CONFIG NAMES llvm-config)
endif()

if(NOT LLVM_CONFIG)
  # Auto-detect Homebrew LLVM even when PATH isn't set up yet
  find_program(BREW_EXECUTABLE brew)
  if(BREW_EXECUTABLE)
    execute_process(
      COMMAND "${BREW_EXECUTABLE}" --prefix llvm
      OUTPUT_VARIABLE BREW_LLVM_PREFIX
      RESULT_VARIABLE BREW_LLVM_RESULT
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(BREW_LLVM_RESULT EQUAL 0 AND EXISTS "${BREW_LLVM_PREFIX}/bin/llvm-config")
      set(LLVM_CONFIG "${BREW_LLVM_PREFIX}/bin/llvm-config" CACHE FILEPATH "Path to llvm-config" FORCE)
    endif()
  endif()
endif()

if(NOT LLVM_CONFIG)
  message(FATAL_ERROR
    "llvm-config not found.\n"
    "  Homebrew:     brew install llvm  (see chapter-04)\n"
    "  Windows:      ensure C:\\Program Files\\LLVM\\bin is on your PATH\n"
    "  Custom build: pass -DLLVM_PREFIX=<path> to cmake")
endif()

message(STATUS "Using llvm-config: ${LLVM_CONFIG}")

# ---------------------------------------------------------------------------
# Query LLVM flags
# ---------------------------------------------------------------------------

set(LLVM_COMPONENTS all)

execute_process(
  COMMAND "${LLVM_CONFIG}" --cxxflags
  OUTPUT_VARIABLE LLVM_CXXFLAGS
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
execute_process(
  COMMAND "${LLVM_CONFIG}" --ldflags --system-libs --libs ${LLVM_COMPONENTS}
  OUTPUT_VARIABLE LLVM_LDFLAGS
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Normalize newline-separated flags to spaces
string(REGEX REPLACE "[\r\n]+" " " LLVM_CXXFLAGS "${LLVM_CXXFLAGS}")
string(REGEX REPLACE "[\r\n]+" " " LLVM_LDFLAGS  "${LLVM_LDFLAGS}")

# Deduplicate linker flags, preserving first-seen order
separate_arguments(_PYXC_LLVM_LDFLAGS NATIVE_COMMAND "${LLVM_LDFLAGS}")
set(_PYXC_LLVM_LDFLAGS_DEDUP)
foreach(_flag IN LISTS _PYXC_LLVM_LDFLAGS)
  if(NOT _flag IN_LIST _PYXC_LLVM_LDFLAGS_DEDUP)
    list(APPEND _PYXC_LLVM_LDFLAGS_DEDUP "${_flag}")
  endif()
endforeach()
list(JOIN _PYXC_LLVM_LDFLAGS_DEDUP " " LLVM_LDFLAGS)

# ---------------------------------------------------------------------------
# Compiler and linker flags
# ---------------------------------------------------------------------------

set(_PYXC_CXX_FLAGS "-g -O3 ${LLVM_CXXFLAGS}")
string(REGEX REPLACE "[\r\n]+" " " _PYXC_CXX_FLAGS "${_PYXC_CXX_FLAGS}")
set(CMAKE_CXX_FLAGS "${_PYXC_CXX_FLAGS}" CACHE STRING "C++ flags" FORCE)

set(_PYXC_EXE_LINKER_FLAGS "${LLVM_LDFLAGS}")
string(REGEX REPLACE "[\r\n]+" " " _PYXC_EXE_LINKER_FLAGS "${_PYXC_EXE_LINKER_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS "${_PYXC_EXE_LINKER_FLAGS}" CACHE STRING "Executable linker flags" FORCE)

# ---------------------------------------------------------------------------
# Optional extra library paths
# ---------------------------------------------------------------------------

set(EXTRA_LIBDIR "" CACHE PATH "Optional extra linker search path")
if(EXTRA_LIBDIR)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -L${EXTRA_LIBDIR} -Wl,-rpath,${EXTRA_LIBDIR}")
endif()

# zstd — try Homebrew prefix if not set explicitly
set(ZSTD_LIBDIR "" CACHE PATH "Optional zstd library directory")
if(NOT ZSTD_LIBDIR)
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
    endif()
  endif()
endif()
if(ZSTD_LIBDIR)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -L${ZSTD_LIBDIR} -Wl,-rpath,${ZSTD_LIBDIR}")
endif()

# lld — try Homebrew prefix if not already covered by llvm-config
# (Homebrew splits lld into a separate keg since LLVM 22)
set(LLD_LIBDIR "" CACHE PATH "Optional lld library directory")
if(NOT LLD_LIBDIR)
  find_program(BREW_EXECUTABLE brew)
  if(BREW_EXECUTABLE)
    execute_process(
      COMMAND "${BREW_EXECUTABLE}" --prefix lld
      OUTPUT_VARIABLE BREW_LLD_PREFIX
      RESULT_VARIABLE BREW_LLD_RESULT
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(BREW_LLD_RESULT EQUAL 0 AND EXISTS "${BREW_LLD_PREFIX}/lib")
      set(LLD_LIBDIR "${BREW_LLD_PREFIX}/lib")
    endif()
  endif()
endif()
if(LLD_LIBDIR)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -L${LLD_LIBDIR} -Wl,-rpath,${LLD_LIBDIR}")
endif()

set(LLD_FLAGS "-llldCommon -llldELF -llldMachO -llldCOFF")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${LLD_FLAGS}")

# ---------------------------------------------------------------------------
# Targets
# ---------------------------------------------------------------------------

add_executable(pyxc pyxc.cpp)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/runtime.c")
  add_library(runtime_obj OBJECT runtime.c)
endif()
