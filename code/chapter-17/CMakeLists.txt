cmake_minimum_required(VERSION 3.16)

# CMake 3.29+ can avoid repeating static libraries when the linker does not
# require it. The Apple linker resolves archives without repetition and warns
# when the same library appears more than once.
if(POLICY CMP0156)
  cmake_policy(SET CMP0156 NEW)
endif()
project(pyxc-chapter-17)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# I use the LLVM installation prepared in Chapter 6.
find_package(LLVM REQUIRED CONFIG)
find_package(LLD REQUIRED CONFIG HINTS "${LLVM_DIR}/../lld" NO_DEFAULT_PATH)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
include_directories(SYSTEM ${LLD_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

add_executable(pyxc pyxc.cpp)

if(NOT LLVM_ENABLE_RTTI)
  target_compile_options(pyxc PRIVATE -fno-rtti)
endif()

llvm_map_components_to_libnames(LLVM_LIBS OrcJIT Passes nativecodegen)
target_link_libraries(pyxc PRIVATE ${LLVM_LIBS} lldCommon lldELF lldMachO lldCOFF)
