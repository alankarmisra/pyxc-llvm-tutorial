# Configure step:
# -S .   -> source dir is current directory (where CMakeLists.txt lives)
# -B build -> generate build files in ./build
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Build step:
# --build build -> compile using the generated build system in ./build
cmake --build build
