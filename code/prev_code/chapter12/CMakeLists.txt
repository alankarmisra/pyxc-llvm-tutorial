cmake_minimum_required(VERSION 3.16)
project(pyxc_chapter12 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(LLVM_PREFIX "$ENV{HOME}/llvm-21-with-clang-lld-lldb-mlir" CACHE PATH "Path to LLVM install prefix")
set(LLVM_CONFIG "${LLVM_PREFIX}/bin/llvm-config" CACHE FILEPATH "Path to llvm-config")

if(NOT EXISTS "${LLVM_CONFIG}")
  find_program(LLVM_CONFIG NAMES llvm-config HINTS "${LLVM_PREFIX}/bin")
endif()

if(NOT LLVM_CONFIG)
  message(FATAL_ERROR "llvm-config not found. Set LLVM_CONFIG or LLVM_PREFIX.")
endif()

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

# llvm-config may emit newline-separated flags; normalize to spaces so the
# linker/compiler command lines remain a single invocation.
string(REGEX REPLACE "[\r\n]+" " " LLVM_CXXFLAGS "${LLVM_CXXFLAGS}")
string(REGEX REPLACE "[\r\n]+" " " LLVM_LDFLAGS "${LLVM_LDFLAGS}")

# Deduplicate linker flags emitted by llvm-config, preserving first-seen order.
separate_arguments(_PYXC_LLVM_LDFLAGS NATIVE_COMMAND "${LLVM_LDFLAGS}")
set(_PYXC_LLVM_LDFLAGS_DEDUP)
foreach(_flag IN LISTS _PYXC_LLVM_LDFLAGS)
  if(NOT _flag IN_LIST _PYXC_LLVM_LDFLAGS_DEDUP)
    list(APPEND _PYXC_LLVM_LDFLAGS_DEDUP "${_flag}")
  endif()
endforeach()
list(JOIN _PYXC_LLVM_LDFLAGS_DEDUP " " LLVM_LDFLAGS)

# Set deterministic flag baselines so repeated configure runs do not keep
# appending duplicate LLVM flags.
set(_PYXC_CXX_FLAGS "-g -O3 ${LLVM_CXXFLAGS}")
string(REGEX REPLACE "[\r\n]+" " " _PYXC_CXX_FLAGS "${_PYXC_CXX_FLAGS}")
set(CMAKE_CXX_FLAGS "${_PYXC_CXX_FLAGS}" CACHE STRING "C++ flags" FORCE)

set(_PYXC_EXE_LINKER_FLAGS "${LLVM_LDFLAGS}")
string(REGEX REPLACE "[\r\n]+" " " _PYXC_EXE_LINKER_FLAGS
                     "${_PYXC_EXE_LINKER_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS "${_PYXC_EXE_LINKER_FLAGS}" CACHE STRING
    "Executable linker flags" FORCE)

set(EXTRA_LIBDIR "" CACHE PATH "Optional extra linker search path")
set(ZSTD_LIBDIR "" CACHE PATH "Optional zstd library directory")

if(EXTRA_LIBDIR)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -L${EXTRA_LIBDIR} -Wl,-rpath,${EXTRA_LIBDIR}")
endif()

# If ZSTD_LIBDIR is not set explicitly, try Homebrew's zstd prefix.
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

set(LLD_FLAGS "-llldCommon -llldELF -llldMachO -llldCOFF")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${LLD_FLAGS}")

add_executable(pyxc pyxc.cpp)

# If runtime.c exists, compile it to runtime.o in the build directory
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/runtime.c")
  add_library(pyxc_runtime OBJECT runtime.c)
  set_target_properties(pyxc_runtime PROPERTIES
    C_STANDARD 11
    POSITION_INDEPENDENT_CODE ON
  )

  # Create a custom command to copy the object file to runtime.o
  add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/runtime.o"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_OBJECTS:pyxc_runtime>" "${CMAKE_CURRENT_BINARY_DIR}/runtime.o"
    DEPENDS pyxc_runtime
    COMMENT "Creating runtime.o"
  )
  add_custom_target(runtime_target ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/runtime.o")

  # Make runtime symbols available in the pyxc process so JIT'd code can
  # resolve externs like putchard/printd via current-process lookup.
  target_sources(pyxc PRIVATE $<TARGET_OBJECTS:pyxc_runtime>)
endif()
