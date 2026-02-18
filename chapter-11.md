---
description: "Add mutable variables with `let` declarations and assignment expressions."
---
# 11. Pyxc: Mutable Variables

## What We're Building

By chapter 10, we can write control flow (`if` / `while`), but we still can’t update local state.

In this chapter, we add:

- variable declaration: `let x = expr`
- assignment: `x = expr`

That unlocks loop-style algorithms like iterative factorial.

```py
def fact_iter(n):
  let acc = 1
  let i = 2
  while i <= n:
    acc = acc * i
    i = i + 1
  return acc
```

## Source Code

Grab the code: [code/chapter11](https://github.com/alankarmisra/pyxc-llvm-tutorial/tree/main/code/chapter11)

Or clone the whole repo:

```bash
git clone https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter11
```

## Grammar Change (EBNF)

```ebnf
primary        = identifierexpr
               | numberexpr
               | parenexpr
               | letexpr
               | ifexpr
               | whileexpr ;

letexpr        = "let" identifier "=" expression ;
identifierexpr = identifier
               | identifier "=" expression
               | identifier "(" [ expression { "," expression } ] ")" ;
```

Notes:

- Assignment is expression-form (`x = expr`) and returns the assigned value.
- `let` introduces a mutable local.

## Implementation

### 1. Lexer and Keywords

Add `let` keyword token (`tok_let`) and include it in keyword/token-name tables.

### 2. New AST Nodes

Add:

- `LetExprAST(Name, InitExpr)`
- `AssignExprAST(Name, ValueExpr)`

### 3. Parser Changes

- `ParsePrimary()` handles `tok_let` via `ParseLetExpr()`.
- `ParseIdentifierExpr()` detects assignment form:
  - `identifier '=' expression` -> `AssignExprAST`
  - otherwise existing variable-ref/call behavior.

### 4. Codegen Changes

To make variables mutable, we store locals in stack slots (`alloca`) and load/store them.

- `NamedValues` now maps to `AllocaInst*` instead of raw SSA value.
- Function arguments are copied into allocas at function entry.
- `VariableExprAST` emits `load`.
- `LetExprAST` emits entry-block `alloca` + `store`.
- `AssignExprAST` emits `store` to existing variable and returns stored value.

### 5. Validation Rules

- Unknown assignment target -> `Unknown variable name in assignment`
- Missing `=` after `let name` -> clear parse error
- Duplicate declaration in same function scope -> `Duplicate variable declaration`

## Compile and Test

```bash
cd code/chapter11
./build.sh
llvm-lit test/
```

## Sample Session

```bash
$ ./build/pyxc repl
ready> def fact_iter(n):
ready>   let acc = 1
ready>   let i = 2
ready>   while i <= n:
ready>     acc = acc * i
ready>     i = i + 1
ready>   return acc
ready> fact_iter(5)
120.000000
```

## Error Cases

Missing `=` in let:

```py
def bad(x):
  let y
  return x
```

Produces:

- `Expected '=' after variable name in let expression`

Assign to unknown variable:

```py
def bad(x):
  y = x + 1
  return x
```

Produces:

- `Unknown variable name in assignment`

## Testing Your Implementation

This chapter includes **79 automated tests**. New mutable-focused tests include:

- `repl_mutable_factorial_iter.pyxc`
- `ir_mutable_let_assign.pyxc`
- `error_let_missing_equal.pyxc`
- `error_assign_unknown_variable.pyxc`

Run:

```bash
cd code/chapter11
llvm-lit -v test/
```

## What We Built

- Mutable local variables (`let`)
- Assignment expressions
- Load/store-based variable codegen
- Iterative algorithms using `while` + mutable state

## What's Next

Next language steps can add richer types and logical operators, while toolchain chapters (16+) continue optimization and native build pipeline work.
