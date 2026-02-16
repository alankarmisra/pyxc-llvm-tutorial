#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
BUILD_TYPE="Debug"
RUN_TEST=0
DO_CLEAN=0
LLVM_CONFIG_PATH=""
LLVM_PREFIX_PATH=""

usage() {
  cat <<'EOF'
Usage: ./build.sh [options]

Options:
  --clean                  Remove build dir before configuring
  --test                   Run ./build/test_bridge after build
  --release                Configure with CMAKE_BUILD_TYPE=Release
  --debug                  Configure with CMAKE_BUILD_TYPE=Debug (default)
  --llvm-config <path>     Pass -DLLVM_CONFIG=<path> to CMake
  --llvm-prefix <path>     Pass -DLLVM_PREFIX=<path> to CMake
  -h, --help               Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --clean)
      DO_CLEAN=1
      shift
      ;;
    --test)
      RUN_TEST=1
      shift
      ;;
    --release)
      BUILD_TYPE="Release"
      shift
      ;;
    --debug)
      BUILD_TYPE="Debug"
      shift
      ;;
    --llvm-config)
      [[ $# -ge 2 ]] || { echo "error: --llvm-config requires a value"; exit 1; }
      LLVM_CONFIG_PATH="$2"
      shift 2
      ;;
    --llvm-prefix)
      [[ $# -ge 2 ]] || { echo "error: --llvm-prefix requires a value"; exit 1; }
      LLVM_PREFIX_PATH="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "error: unknown argument: $1"
      usage
      exit 1
      ;;
  esac
done

if [[ "${DO_CLEAN}" -eq 1 ]]; then
  rm -rf "${BUILD_DIR}"
fi

cmake_args=(
  -S "${SCRIPT_DIR}"
  -B "${BUILD_DIR}"
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
)

if [[ -n "${LLVM_CONFIG_PATH}" ]]; then
  cmake_args+=("-DLLVM_CONFIG=${LLVM_CONFIG_PATH}")
fi

if [[ -n "${LLVM_PREFIX_PATH}" ]]; then
  cmake_args+=("-DLLVM_PREFIX=${LLVM_PREFIX_PATH}")
fi

echo "==> Configuring (${BUILD_TYPE})"
cmake "${cmake_args[@]}"

echo "==> Building"
cmake --build "${BUILD_DIR}" -j

if [[ "${RUN_TEST}" -eq 1 ]]; then
  echo "==> Running test_bridge"
  "${BUILD_DIR}/test_bridge"
fi

echo "==> Done"
