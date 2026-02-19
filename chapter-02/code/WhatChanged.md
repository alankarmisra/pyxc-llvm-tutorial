# Chapter 02 - What Changed

## Features Added
- Added parser + AST on top of the chapter-01 lexer.
- Added top-level parsing for:
  - function definitions (`def ... : return ...`)
  - extern declarations (`extern def ...`)
  - top-level expressions
- Added expression parsing with operator precedence for:
  - `<`, `>`, `+`, `-`, `*`, `/`, `%`
- Added parsing for:
  - number literals
  - identifier references
  - call expressions (including nested calls)
  - parenthesized expressions

## EBNF Changes
- Updated `pyxc.ebnf` from lexer baseline to chapter-2 grammar:
  - program with top-level forms
  - prototypes
  - function definitions with `return`
  - expression grammar with binary operators and primaries

## Tests Added
- Added comprehensive `c02_*` parser/AST test suite:
  - defs/externs/calls/identifiers/numbers
  - precedence/parentheses/comparisons
  - parse errors and recovery

## Tests Modified
- Kept `c01_token_invalid_number.pyxc` as a portability check for lexer diagnostics in chapter 2.
- Adjusted `c02_parse_error.pyxc` to assert parser diagnostic + caret context without requiring exact source-line echo for EOF-terminated input.

## Tests Removed
- Removed chapter-01 token-stream focused tests from chapter-02 code directory copy, because chapter-02 runtime behavior is parser-first rather than token-dump-first.
