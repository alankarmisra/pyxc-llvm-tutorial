#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PY="$ROOT/bench/cases/particles_heavy.py"
PYXC_SRC="$ROOT/bench/cases/particles_heavy.pyxc"
PYXC_BIN="$ROOT/bench/cases/particles_heavy"
RUNS="${1:-3}"

if [[ ! -x "$ROOT/pyxc" ]]; then
  echo "Building pyxc first..."
  make -C "$ROOT" pyxc >/dev/null
fi

echo "Compiling pyxc benchmark target..."
(
  cd "$ROOT"
  rm -f "$PYXC_BIN" bench/cases/particles_heavy.o
  "$ROOT/pyxc" "$PYXC_SRC" >/dev/null 2>&1
)

extract_last_number() {
  awk '/^-?[0-9]+(\.[0-9]+)?$/{v=$0} END{print v}'
}

echo "Checking outputs match..."
py_out="$(python3 "$PY" | extract_last_number)"
pyxc_i_out="$("$ROOT/pyxc" -i "$PYXC_SRC" 2>&1 | extract_last_number)"
pyxc_x_out="$("$PYXC_BIN" 2>&1 | extract_last_number)"

echo "python:          $py_out"
echo "pyxc -i:         $pyxc_i_out"
echo "pyxc executable: $pyxc_x_out"

if [[ "$py_out" != "$pyxc_i_out" || "$py_out" != "$pyxc_x_out" ]]; then
  echo "ERROR: Outputs do not match."
  exit 1
fi

bench_cmd() {
  local name="$1"
  local cmd="$2"
  local times=()
  local t

  echo
  echo "$name"
  for i in $(seq 1 "$RUNS"); do
    t="$(/usr/bin/time -p sh -c "$cmd >/dev/null 2>&1" 2>&1 | awk '/^real /{print $2}')"
    times+=("$t")
    echo "  run $i: ${t}s"
  done

  printf '%s\n' "${times[@]}" | awk '
    {sum+=$1; if(NR==1||$1<min) min=$1; if(NR==1||$1>max) max=$1}
    END {printf("  avg: %.6fs  min: %.6fs  max: %.6fs\n", sum/NR, min, max)}
  '
}

echo
echo "Benchmarking ($RUNS runs each)..."
bench_cmd "Python (regular interpreter)" "python3 \"$PY\""
bench_cmd "pyxc (interpreter mode)" "\"$ROOT/pyxc\" -i \"$PYXC_SRC\""
bench_cmd "pyxc (compiled executable runtime)" "\"$PYXC_BIN\""
