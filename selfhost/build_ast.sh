#!/usr/bin/env bash
set -e

PYXC="../code/chapter27/build/pyxc"
RUNTIME="../code/chapter27/build/CMakeFiles/runtime_obj.dir/runtime.c.o"

echo "==> Compiling ast.pyxc"
$PYXC -c ast.pyxc -o build/ast.o

echo "==> Compiling test_ast.pyxc"
$PYXC -c test_ast.pyxc -o build/test_ast.o

echo "==> Linking test_ast"
SDKROOT="$(xcrun --sdk macosx --show-sdk-path)"
xcrun --sdk macosx clang -isysroot "$SDKROOT" build/test_ast.o build/ast.o $RUNTIME -o build/test_ast

echo "==> Running test_ast"
./build/test_ast

echo ""
echo "==> AST tests complete!"
