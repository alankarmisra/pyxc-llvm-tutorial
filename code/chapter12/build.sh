# Configure step:
# -S .   -> source dir is current directory (where CMakeLists.txt lives)
# -B build -> generate build files in ./build
cmake -S . -B build

# Build step:
# --build build -> compile using the generated build system in ./build
cmake --build build
