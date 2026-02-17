# Chapter 25 Design Requirements

## Theme
Immutable value bindings via `const` declarations.

## Goal
Add first-class constant declarations to Pyxc so values can be explicitly immutable after initialization.

## Scope

### In Scope
- `const` declaration statement syntax:
  - `const name: type = expression`
- Requires initializer expression
- Binds immutable variable in current scope
- Assignment to const variable is rejected
- Works with scalar/pointer/aggregate-compatible declaration types

### Out of Scope
- Compile-time constant folding requirements for initializer
- Global const linkage model
- Deep immutability of pointee/aggregate contents

## Syntax Requirements
```py
const x: i32 = 42
const p: ptr[i8] = "hello"
```

## Lexer Requirements
- Add `const` keyword token.

## Parser Requirements
- Add parser for const declaration statement.
- `const` declarations are statement-level (same scope model as local typed declarations).

## AST Requirements
- Add `ConstAssignStmtAST(Name, DeclType, InitExpr)`.

## Semantic Requirements
- `const` declarations must provide initializer.
- Reassignment to const variable name is diagnostic error.
- Existing typed variable behavior unchanged for non-const declarations.

## Diagnostics Requirements
- Missing const initializer.
- Assignment to const variable.

## Tests

### Positive
- const scalar usage
- const pointer usage
- const in arithmetic/branch conditions

### Negative
- const declaration without initializer
- assignment to const variable

## Done Criteria
- Chapter 25 lit suite includes const coverage and passes.
- Chapter 24 behavior remains green.
- Chapter 25 docs/chapter summary added.
