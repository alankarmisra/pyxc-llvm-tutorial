#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CASES_DIR="$ROOT/bench/cases"
RUNS="${1:-3}"
CASES=("particles" "particles_heavy" "lcg_hash")

if [[ ! -x "$ROOT/pyxc" ]]; then
  echo "Building pyxc first..."
  make -C "$ROOT" pyxc >/dev/null
fi

extract_last_number() {
  awk '/^-?[0-9]+(\.[0-9]+)?$/{v=$0} END{print v}'
}

measure_cmd() {
  local cmd="$1"
  local times=()
  local t
  for _ in $(seq 1 "$RUNS"); do
    t="$(/usr/bin/time -p sh -c "$cmd >/dev/null 2>&1" 2>&1 | awk '/^real /{print $2}')"
    times+=("$t")
  done

  printf '%s\n' "${times[@]}" | awk '
    {sum+=$1; if(NR==1||$1<min) min=$1; if(NR==1||$1>max) max=$1}
    END {printf("%.6f %.6f %.6f", sum/NR, min, max)}
  '
}

printf "\n%-12s %-12s %-26s %-26s %-26s\n" "case" "result" "python (avg/min/max s)" "pyxc -i (avg/min/max s)" "pyxc exe (avg/min/max s)"
printf "%-12s %-12s %-26s %-26s %-26s\n" "------------" "------------" "--------------------------" "--------------------------" "--------------------------"

for case in "${CASES[@]}"; do
  py="$CASES_DIR/$case.py"
  pyxc_src="$CASES_DIR/$case.pyxc"
  pyxc_bin="$CASES_DIR/$case"
  pyxc_obj="$CASES_DIR/$case.o"

  rm -f "$pyxc_bin" "$pyxc_obj"
  (
    cd "$ROOT"
    "$ROOT/pyxc" "bench/cases/$case.pyxc" >/dev/null 2>&1
  )

  py_out="$(python3 "$py" | extract_last_number)"
  pyxc_i_out="$("$ROOT/pyxc" -i "$pyxc_src" 2>&1 | extract_last_number)"
  pyxc_x_out="$("$pyxc_bin" 2>&1 | extract_last_number)"

  result="ok"
  if [[ "$py_out" != "$pyxc_i_out" || "$py_out" != "$pyxc_x_out" ]]; then
    result="mismatch"
  fi

  py_stats="$(measure_cmd "python3 \"$py\"")"
  pyxc_i_stats="$(measure_cmd "\"$ROOT/pyxc\" -i \"$pyxc_src\"")"
  pyxc_x_stats="$(measure_cmd "\"$pyxc_bin\"")"

  printf "%-12s %-12s %-26s %-26s %-26s\n" \
    "$case" \
    "$result" \
    "$py_stats" \
    "$pyxc_i_stats" \
    "$pyxc_x_stats"
done

echo
echo "Runs per mode: $RUNS"
echo "Stats format: avg min max (seconds)"
