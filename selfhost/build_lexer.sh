#!/usr/bin/env bash
# Build script for Phase 1 - Lexer

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PYXC="${SCRIPT_DIR}/../code/chapter27/build/pyxc"
RUNTIME_OBJ="${SCRIPT_DIR}/../code/chapter27/build/runtime.o"
RUNTIME_OBJ_ALT="${SCRIPT_DIR}/../code/chapter27/build/CMakeFiles/runtime_obj.dir/runtime.c.o"

if [ ! -f "${PYXC}" ]; then
  echo "Error: pyxc not found at ${PYXC}"
  echo "Build chapter27 first:"
  echo "  cd ../code/chapter27"
  echo "  cmake -B build && cmake --build build"
  exit 1
fi

# pyxc expects runtime.o in build/, but CMake may place it under CMakeFiles/.
if [ ! -f "${RUNTIME_OBJ}" ]; then
  cmake --build "${SCRIPT_DIR}/../code/chapter27/build" --target runtime.o >/dev/null 2>&1 || true
fi
if [ ! -f "${RUNTIME_OBJ}" ] && [ -f "${RUNTIME_OBJ_ALT}" ]; then
  cp "${RUNTIME_OBJ_ALT}" "${RUNTIME_OBJ}"
fi

echo "==> Compiling lexer.pyxc"
"${PYXC}" "${SCRIPT_DIR}/lexer.pyxc" -o "${SCRIPT_DIR}/lexer_test"

if [ $? -eq 0 ]; then
  echo "==> Compilation successful!"
  echo "==> Running lexer test..."
  "${SCRIPT_DIR}/lexer_test"
else
  echo "==> Compilation failed!"
  exit 1
fi
