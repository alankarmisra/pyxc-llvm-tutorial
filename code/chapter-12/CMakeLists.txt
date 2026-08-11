cmake_minimum_required(VERSION 3.16)
project(pyxc-chapter-12)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# I use the LLVM installation prepared in Chapter 6.
find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

add_executable(pyxc pyxc.cpp)

if(NOT LLVM_ENABLE_RTTI)
  target_compile_options(pyxc PRIVATE -fno-rtti)
endif()

llvm_map_components_to_libnames(LLVM_LIBS OrcJIT Passes nativecodegen)
target_link_libraries(pyxc PRIVATE ${LLVM_LIBS})
