#!/usr/bin/env bash
set -euo pipefail
set +H

ROOT="$(cd "$(dirname "$0")" && pwd)"
PYXC_BIN="$ROOT/../chapter-16/build/pyxc"

# Build binaries
"$PYXC_BIN" --emit exe -O3 -o "$ROOT/mandel_pyxc" "$ROOT/mandel_pyxc.pyxc"

xcrun clang -O3 -ffp-contract=off "$ROOT/mandel.c" -o "$ROOT/mandel_c"
xcrun clang++ -O3 -ffp-contract=off "$ROOT/mandel.cpp" -o "$ROOT/mandel_cpp"
rustc -O "$ROOT/mandel.rs" -o "$ROOT/mandel_rs"

run_times() {
  local label="$1"
  shift
  local cmd=("$@")
  local tmp
  tmp="$(mktemp)"
  echo "$label"
  for i in 1 2 3 4 5; do
    /usr/bin/time -p "${cmd[@]}" 2>"$tmp" >/dev/null
    awk '/^real /{print $2}' "$tmp"
  done | awk '
    { a[NR] = $1 }
    END {
      n = NR
      # sort
      for (i = 1; i <= n; ++i) {
        for (j = i + 1; j <= n; ++j) {
          if (a[j] < a[i]) { t = a[i]; a[i] = a[j]; a[j] = t }
        }
      }
      median = a[(n+1)/2]
      printf("median: %.2f\n\n", median)
    }
  '
  rm -f "$tmp"
}

run_times "Python" python3 "$ROOT/mandel_py.py"
run_times "Pyxc" "$ROOT/mandel_pyxc"
run_times "C" "$ROOT/mandel_c"
run_times "C++" "$ROOT/mandel_cpp"
run_times "Rust" "$ROOT/mandel_rs"
