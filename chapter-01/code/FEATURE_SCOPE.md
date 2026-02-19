# Chapter 01 Feature Scope

## Source of truth
- Reference implementation inspected from:
  - `code/pyxc_ref_code/chapter01/pyxc.cpp`
  - `code/pyxc_ref_code/chapter01/pyxc.ebnf`
  - `code/pyxc_ref_code/chapter01/test/`

## Chapter goal
Implement the lexer and token-stream driver for Pyxc so source text is tokenized reliably with clear diagnostics and line-aware behavior.

## In-scope features
1. Core tokenization
- Keywords: `def`, `extern`, `return`
- Identifiers: letters/underscore + alnum/underscore tail
- Numbers: integer/float forms accepted by chapter 1 lexer rules
- Operators/punctuation as single-char tokens
- End-of-line token handling

2. Source handling
- Whitespace skipping (except newline)
- Comment handling (`# ...` to end of line)
- Line ending support: LF, CRLF, CR normalization

3. Diagnostics
- Invalid number literal diagnostics
- Line/column location reporting
- Source line + caret context for errors

4. Token stream output mode
- REPL/token dump behavior for lexer validation
- Stable token naming for tests

## EBNF target (chapter 1)
- Maintain grammar equivalent to `code/pyxc_ref_code/chapter01/pyxc.ebnf`.
- Chapter 1 is lexer-first; parser/codegen features are out of scope.

## Out of scope
- AST construction
- Parser for defs/extern/expressions
- LLVM IR generation
- JIT/runtime execution
- Build/run compilation modes beyond lexer-focused flow

## Deliverables
1. `code/chapter01/pyxc.cpp`
- Lexer implementation + token stream driver.

2. `code/chapter01/pyxc.ebnf`
- Chapter-1 grammar baseline.

3. `code/chapter01/test/`
- Lexer-focused tests using chapter prefix convention (`c01_*`).

4. `code/chapter01/WhatChanged.md`
- Reader-facing chapter-1 implementation summary.

## Test scope requirements
- Positive coverage:
  - keyword tokens
  - identifier variants (including underscore)
  - numbers (int/float)
  - operators/punctuation
  - comments and whitespace
  - line-ending variants (LF/CRLF/CR)

- Negative coverage:
  - malformed numeric literal handling
  - tokenization behavior around comment/line boundaries

## Acceptance criteria
1. Chapter 1 builds cleanly.
2. Chapter 1 lexer test suite passes under `llvm-lit`.
3. Invalid literals produce actionable diagnostics with source location.
4. Token stream behavior matches chapter-1 reference intent without copy-pasting code.

## Implementation notes
- Keep lexer and diagnostics code modular to support parser integration in chapter 2.
- Preserve deterministic token naming/output for stable tests.
