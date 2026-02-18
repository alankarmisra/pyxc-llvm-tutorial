#!/usr/bin/env bash

set -u

code_root="$(cd "$(dirname "$0")" && pwd)"
failed_chapters=()

shopt -s nullglob
chapter_dirs=("$code_root"/chapter*/)
shopt -u nullglob

if [[ ${#chapter_dirs[@]} -eq 0 ]]; then
  echo "No chapter*/ directories found in: $code_root"
  exit 1
fi

for dir in "${chapter_dirs[@]}"; do
  chapter="$(basename "$dir")"
  echo "=== $chapter ==="

  (
    cd "$dir" || exit 1

    if [[ ! -f ./build.sh ]]; then
      echo "Missing build.sh in $chapter"
      exit 1
    fi

    bash ./build.sh
    llvm-lit test/
  )

  status=$?
  if [[ $status -ne 0 ]]; then
    failed_chapters+=("$chapter")
    echo "FAILED: $chapter"
  else
    echo "PASSED: $chapter"
  fi

  echo
done

if [[ ${#failed_chapters[@]} -gt 0 ]]; then
  echo "Chapters with failed tests:"
  for chapter in "${failed_chapters[@]}"; do
    echo "- $chapter"
  done
  exit 1
fi

echo "All chapters passed."
