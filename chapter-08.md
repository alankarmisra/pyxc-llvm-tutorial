---
description: "Add file-based execution so Pyxc can run source from a script file, not just interactive stdin."
---
# 8. Pyxc: File Input Mode

## What We're Building

In Chapter 7, we added JIT execution in the REPL. In this chapter, we add real file input for:

- `pyxc run script.pyxc`

This keeps the same parser + JIT pipeline, but swaps lexer input from `stdin` to a `FILE*`.

## Source Code

Grab the code: [code/chapter08](https://github.com/alankarmisra/pyxc-llvm-tutorial/tree/main/code/chapter08)

## Design

The key change is to stop hardcoding `getchar()`/`stdin` in the lexer.

- Add `LexInput` (`FILE*`) and read characters with `fgetc(LexInput)`.
- Keep CRLF normalization and diagnostics exactly the same.
- Add `ResetLexerState()` so REPL and `run` start with clean location/token state.

`run` mode now:

1. validates one file path
2. opens the file with `fopen`
3. sets `LexInput` to that file
4. runs normal parse/JIT loop in non-interactive mode
5. closes the file

## Sample

```bash
cat > sample.pyxc <<'PYXC'
def square(x):
  return x * x

square(6)
PYXC

./build/pyxc run sample.pyxc
# 36.000000
```

## Error Handling

- Missing file name:
  - `Error: run requires a file name.`
- File open failure:
  - `Error: could not open file '...'.`

## Testing Your Implementation

This chapter includes **48 automated tests**.

New file-mode focused tests:

- `cli_run_exec_file.pyxc`
- `cli_run_emit_ir.pyxc`
- `cli_run_missing_file.pyxc`
- `cli_run_requires_file.pyxc`

Run:

```bash
cd code/chapter08
./build.sh
llvm-lit -v test/
```
