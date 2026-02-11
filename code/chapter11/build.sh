#!/usr/bin/env bash
set -euo pipefail

# Load project-local build settings.
set -a
source "$(dirname "$0")/.env"
set +a

# Optional runtime support object used by pyxc executable builds.
if [[ -f runtime.c ]]; then
  /usr/bin/clang -c runtime.c -o runtime.o
fi

/usr/bin/clang++ -g -O3 pyxc.cpp \
  `$LLVM_PREFIX/bin/llvm-config --cxxflags --ldflags --system-libs --libs all` \
  -L$HOMEBREW_LIB \
  -Wl,-rpath,$HOMEBREW_LIB \
  -L$LLVM_PREFIX/lib \
  -llldCommon -llldELF -llldMachO -llldCOFF \
  -o pyxc
