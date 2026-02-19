# Chapter 04 - What Changed

## Features Added
- Added structured CLI with LLVM command-line parsing:
  - `repl`, `run`, and `build` subcommands.
- Added output mode flags:
  - `repl --emit=tokens|llvm-ir`
  - `run --emit=llvm-ir`
  - `build --emit=llvm-ir|obj|exe`
- Added `build` flags:
  - `-g`
  - `-O0` to `-O3` (with validation and clear error for invalid levels).
- Added explicit chapter-04 stub behavior for not-yet-implemented modes:
  - `run: i havent learnt how to do that yet.`
  - `build: i havent learnt how to do that yet.`
  - `repl --emit=llvm-ir: i havent learnt how to do that yet.`

## EBNF Changes
- No language grammar change from chapter 2.
- CLI behavior was added at the driver layer (`main`), not parser grammar.

## Tests Added
- Added `c04_*` CLI tests:
  - `c04_cli_repl_emit_tokens.pyxc`
  - `c04_cli_repl_emit_llvm_ir_stub.pyxc`
  - `c04_cli_run_missing_file.pyxc`
  - `c04_cli_run_too_many_files.pyxc`
  - `c04_cli_run_emit_llvm_ir_stub.pyxc`
  - `c04_cli_build_missing_file.pyxc`
  - `c04_cli_build_too_many_files.pyxc`
  - `c04_cli_build_invalid_opt_level.pyxc`

## Tests Modified
- Updated `test/lit.cfg.py` suite name to `pyxc-chapter04`.

## Tests Removed
- None.
