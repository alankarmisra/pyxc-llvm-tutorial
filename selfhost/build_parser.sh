#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PYXC="${SCRIPT_DIR}/../code/chapter27/build/pyxc"

if [ ! -f "${PYXC}" ]; then
  echo "Error: pyxc not found at ${PYXC}"
  echo "Build chapter27 first:"
  echo "  cmake -S ../code/chapter27 -B ../code/chapter27/build"
  echo "  cmake --build ../code/chapter27/build"
  exit 1
fi

echo "==> Running parser.pyxc"
"${PYXC}" -i "${SCRIPT_DIR}/parser.pyxc"
