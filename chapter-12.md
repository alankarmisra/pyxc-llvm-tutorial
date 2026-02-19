---
description: "Add indentation-based if/while expressions so Pyxc can express real control flow (including recursive factorial)."
---
# 12. Pyxc: `if` and `while`

## What We're Building

By chapter 11, Pyxc has indentation blocks and comparison operators, but no control flow.

In this chapter, we add control-flow expressions:

- `if ... elif ... else ...`
- `while ...`

Example:

```py
def fact(n):
  return if n < 2:
    1
  else:
    n * fact(n - 1)
```

That gives us a real factorial function in the language.

## Source Code

Grab the code: [code/chapter12](https://github.com/alankarmisra/pyxc-llvm-tutorial/tree/main/code/chapter12)

Or clone the whole repo:

```bash
git clone https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter12
```

## Grammar Change (EBNF)

```ebnf
primary        = identifierexpr
               | numberexpr
               | parenexpr
               | ifexpr
               | whileexpr ;

ifexpr         = "if" expression ":" newline suite
               { "elif" expression ":" newline suite }
               "else" ":" newline suite ;
whileexpr      = "while" expression ":" newline suite ;
suite          = indent expression { newline expression } dedent ;
```

Notes:

- `if` supports zero or more `elif` branches, and still requires a final `else`.
- `while` body is an expression suite.
- Leading indentation can use spaces or tabs; sibling statements in a block must
  use the same indentation prefix.
- `return` is still only valid in function body blocks.

## Implementation

### 1. Add Control-Flow Tokens

Add lexer/parser tokens and keywords:

- `tok_if`
- `tok_elif`
- `tok_else`
- `tok_while`

### 2. Add New AST Nodes

We introduce:

- `SuiteExprAST`: an indented list of expressions, value is final expression.
- `IfExprAST`: condition + then suite + else suite.
- `WhileExprAST`: condition + body suite.

### 3. Parse Indented Suites for Expressions

`ParseIndentedExprSuite()` handles:

- required newline after `:`
- required `INDENT ... DEDENT`
- newline-separated expressions
- erroring on `return` inside if/while suites

### 4. Parse `if`/`elif` Expressions

`ParseIfExpr()` parses:

1. `if <expr> : <suite>`
2. zero or more `elif <expr> : <suite>`
3. required `else : <suite>`

`ParsePrimary()` now dispatches `tok_if` to this function.

### 5. Parse `while` Expressions

`ParseWhileExpr()` parses:

1. `while <expr> : <suite>`

`ParsePrimary()` dispatches `tok_while` here.

### 6. LLVM Codegen

- `IfExprAST` emits:
  - condition compare-to-zero (`fcmp one`)
  - then/else blocks
  - merge block with `phi double`
- `WhileExprAST` emits:
  - `while.cond`, `while.body`, `while.end`
  - loop backedge from body to cond
  - returns `0.0` after loop completes

## Compile and Test

```bash
cd code/chapter12
./build.sh
llvm-lit test/
```

## Sample Session

```bash
$ ./build/pyxc repl
ready> def fact(n):
ready>   return if n < 2:
ready>     1
ready>   elif n < 5:
ready>     n * 2
ready>   else:
ready>     n * fact(n - 1)
ready> fact(5)
120.000000
```

## Error Cases

Missing `else`:

```py
def bad(x):
  return if x > 0:
    x
```

Produces:

- `Expected 'else' in if expression`

Missing `:` in while:

```py
def bad(x):
  return while x < 1
    x + 1
```

Produces:

- `Expected ':' after while condition`

Missing indented while body:

```py
def bad(x):
  return while x < 1:
  x + 1
```

Produces:

- `Expected indented block after ':'`

## Testing Your Implementation

This chapter includes **79 automated tests**. Key new tests:

- `repl_if_factorial.pyxc`
- `repl_if_elif_chain.pyxc`
- `repl_tabs_indent.pyxc`
- `ir_if_expr.pyxc`
- `repl_while_zero.pyxc`
- `ir_while_expr.pyxc`
- `error_if_missing_else.pyxc`
- `error_elif_missing_colon.pyxc`
- `error_mixed_tab_space_indent.pyxc`
- `error_while_missing_colon.pyxc`

Run:

```bash
cd code/chapter12
llvm-lit -v test/
```

## What We Built

- First real control flow in Pyxc (`if` / `while`)
- Recursive factorial in-language
- Indentation-based suites for control-flow expressions
- LLVM IR control-flow construction (`br`, `phi`, loop CFG)

## What's Next

Next, we can add mutability (`let` + assignment) and then return to optimization/toolchain chapters with more meaningful programs.
