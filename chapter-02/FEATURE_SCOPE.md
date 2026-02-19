# Chapter 02 Feature Scope

## Source of truth
- Reference implementation inspected from:
  - `code/pyxc_ref_code/chapter02/pyxc.cpp`
  - `code/pyxc_ref_code/chapter02/pyxc.ebnf`
  - `code/pyxc_ref_code/chapter02/test/`

## Chapter goal
Implement a parser + AST pipeline on top of chapter 1 lexer so Pyxc can parse and validate top-level language forms with robust diagnostics.

## In-scope features
1. Top-level forms
- Function definitions: `def name(args): <expr or return expr>`
- Extern declarations: `extern def name(args)`
- Top-level expressions

2. Expression parsing
- Primary expressions: identifiers, numbers, parenthesized expressions
- Call expressions: `foo(a, b)`
- Binary expressions with precedence: `<`, `>`, `+`, `-`, `*`, `/`, `%`

3. AST model
- Number AST
- Variable AST
- Binary AST
- Call AST
- Prototype AST
- Function AST

4. Parser diagnostics and recovery
- Missing `def` after `extern`
- Missing `:` in definitions
- Missing `return` in function body position (per chapter grammar)
- Missing `)` and malformed parameter/argument lists
- Unexpected tokens in primary expressions
- Line/column anchored errors with source context

5. REPL/top-level driver behavior
- Parse loop handling: definition / extern / top-level expression / newline
- Continue after parse errors by syncing at line boundary

## EBNF target (chapter 2)
- Maintain grammar equivalent to `code/pyxc_ref_code/chapter02/pyxc.ebnf`.
- Keep chapter 2 syntax surface only; no IR/JIT/runtime semantics in this chapter scope.

## Out of scope
- LLVM IR generation
- JIT execution
- Object/executable output
- Mutable variables, control flow statements (`if`, `for`, `while`), blocks, types, structs, memory features

## Deliverables
1. `code/chapter02/pyxc.cpp`
- Parser + AST integrated with chapter-1 lexer behavior.

2. `code/chapter02/pyxc.ebnf`
- Chapter-2 grammar snapshot.

3. `code/chapter02/test/`
- Cumulative runnable test set for chapter 2 conventions.
- Prefixing aligned with chapter policy (`cNN_...`).

4. `code/chapter02/WhatChanged.md`
- Reader-facing delta from chapter 1.

## Test scope requirements
- Positive coverage:
  - Simple/zero/multi-arg defs
  - Extern single/multi
  - Number/identifier/parentheses expressions
  - Call simple/nested/in-expression
  - Operator precedence + comparisons

- Negative coverage:
  - Missing colon / missing return / missing paren
  - Bad call arg list
  - Extern missing `def`
  - Unexpected token
  - Parse recovery across lines

## Acceptance criteria
1. Chapter 2 builds cleanly.
2. Chapter 2 test suite passes under `llvm-lit`.
3. Error messages include actionable source location context.
4. Parser behavior matches chapter 2 reference intent without copying code verbatim.

## Implementation notes
- Reuse architecture patterns from chapter 1 where possible.
- Keep chapter 2 isolated and incremental from chapter 1.
- Preserve readable boundaries between lexer, parser, AST, and driver loop code.
