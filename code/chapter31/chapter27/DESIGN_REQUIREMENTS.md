# Chapter 28 Design Requirements

## Theme
Python-style `match/case` control flow.

## Goal
Add a readable multi-branch statement form without introducing C-style `switch` syntax.

## Scope

### In Scope
- New statement syntax:
  - `match <expr>:`
  - one or more `case <expr>:` clauses
  - optional wildcard default `case _:`
- Case clause suites use existing suite rules (inline or indented block).
- `match` expression must be integer-typed.
- Case expressions must be integer-typed.
- First matching case executes; no fallthrough.

### Out of Scope
- Pattern matching on structs/arrays/strings.
- Guard clauses (`case x if ...`).
- Destructuring or capture patterns.
- Exhaustiveness checks.

## Diagnostics
- Missing `case` inside `match` block should error clearly.
- Duplicate default (`case _`) should error.
- Non-integer match/case expressions should error.

## Tests

### Positive
- integer match with multiple cases and wildcard default
- integer match with no default where no case matches
- nested statements inside case suites

### Negative
- non-integer match expression
- non-integer case expression
- duplicate `case _`

## Done Criteria
- Chapter 28 builds.
- Chapter 28 tests pass.
- Existing Chapter 27 behavior remains green.
