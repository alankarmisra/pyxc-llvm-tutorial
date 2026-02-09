#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")" && pwd)"
bin="${1:-../pyxc}"

tmpdir="${TMPDIR:-/tmp}/pyxc-tests"
mkdir -p "$tmpdir"

if [[ ! -x "$bin" ]]; then
  echo "ERROR: pyxc binary not found at $bin" >&2
  exit 1
fi

failures=0

run_case() {
  local file="$1"
  local expect="$2" # ok | fail
  local name
  name="$(basename "$file")"
  local out
  out="$tmpdir/${name}.out"

  "$bin" -i "$file" >"$out" 2>&1 || true

  if [[ "$expect" == "ok" ]]; then
    if rg -q "Error" "$out"; then
      echo "FAIL: $name (expected success, saw error)"
      cat "$out"
      failures=$((failures+1))
    else
      echo "PASS: $name"
    fi
  else
    if rg -q "Error" "$out"; then
      echo "PASS: $name"
    else
      echo "FAIL: $name (expected failure, no error)"
      cat "$out"
      failures=$((failures+1))
    fi
  fi
}

# Pass cases
for f in "$root"/if_*.pyxc "$root"/for_*.pyxc; do
  [[ -e "$f" ]] || continue
  echo "RUN: $(basename "$f")"
  run_case "$f" ok
done

# Fail cases
for f in "$root"/bad_*.pyxc; do
  [[ -e "$f" ]] || continue
  echo "RUN: $(basename "$f")"
  run_case "$f" fail
done

if [[ $failures -ne 0 ]]; then
  echo "FAILED: $failures test(s)"
  exit 1
fi

echo "ALL PASS"
