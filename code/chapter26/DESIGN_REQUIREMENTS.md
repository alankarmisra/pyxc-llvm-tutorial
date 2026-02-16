# Chapter 25 Design Requirements

## Theme
Separate compilation and multi-file linking workflow.

## Goal
Allow Pyxc to compile and link multiple translation units in one command for executable and object workflows.

## Scope

### In Scope
- Accept multiple positional input files.
- Executable mode links all input translation units (+ runtime object).
- Object mode compiles each input to a separate object file.
- Token and interpret modes remain single-file only with clear diagnostics.

### Out of Scope
- Full preprocessor/include implementation.
- Dependency graph/build system integration.
- Symbol visibility attributes.

## CLI Requirements
- Executable mode:
  - `pyxc file1.pyxc file2.pyxc`
  - `pyxc -o app file1.pyxc file2.pyxc`
- Object mode:
  - `pyxc -c file1.pyxc file2.pyxc`

## Linker Requirements
- Linker helper accepts multiple object files.
- Runtime object remains linked as before.

## Diagnostics Requirements
- interpret mode with multiple files -> error
- token mode with multiple files -> error
- object mode with `-o` and multiple files -> error

## Tests

### Positive
- two-file executable link with `extern def` declaration
- multi-file object compilation emits both object paths

### Negative
- token mode with multiple files errors

## Done Criteria
- Chapter 25 tests pass with multi-file support.
- Chapter 24 behavior remains green.
- Chapter docs updated.
