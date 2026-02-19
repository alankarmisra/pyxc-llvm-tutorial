---
description: "Replace semicolon-separated blocks with indentation-based blocks using INDENT/DEDENT tokenization."
---
# 11. Pyxc: Indentation-Based Blocks

## What We're Building

In Chapter 10, blocks were explicit and semicolon-separated:

```python
def add2(x):
  x + 1;
  return x + 2
```

In this chapter, we keep the same **block semantics** (prefix statements + final `return`) but switch to **indentation syntax**:

```python
def add2(x):
  x + 1
  return x + 2
```

So the parser no longer depends on `;` for statement boundaries inside function bodies.

## Source Code

Grab the code: [code/chapter09](https://github.com/alankarmisra/pyxc-llvm-tutorial/tree/main/code/chapter09)

Or clone the whole repo:

```bash
git clone https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter09
```

## Grammar Change (EBNF)

```ebnf
definition     = "def" prototype ":" newline block ;
block          = indent statement { newline statement } dedent ;
statement      = "return" expression
               | expression ;
```

This means:

- `:` must be followed by a newline.
- A function body starts with `INDENT` and ends with `DEDENT`.
- Statements are newline-separated.
- A block still requires at least one `return`.

## Implementation

### 1. Add Indentation Tokens

The lexer now emits:

- `tok_indent`
- `tok_dedent`

These are synthetic tokens produced from leading spaces at line start.

### 2. Lexer: Indentation Stack + Pending Tokens

At line start, the lexer measures indentation width and compares it to a stack:

- Greater indent -> emit `INDENT` and push
- Smaller indent -> emit one or more `DEDENT` and pop
- Same indent -> emit nothing

To support multiple dedents before a real token, we queue synthetic tokens in `PendingTokens`.

We also added a strict indentation error:

- `inconsistent indentation`

### 3. Parser: Definition Requires Newline After `:`

`ParseDefinition()` now enforces:

- `def ... :` then newline
- then an indented block

So one-line definitions like `def f(x): return x` are rejected in Chapter 11.

### 4. Parser: ParseBlockExpr Consumes INDENT/DEDENT

`ParseBlockExpr()` now:

1. Requires `tok_indent`
2. Parses newline-delimited statements
3. Requires at least one `return` statement
4. Requires `return` to be final
5. Ends on `tok_dedent`

We also emit clearer syntax errors for bare `=` / `!` in block statements:

- `Unexpected '=' when expecting an expression`
- `Unexpected '!' when expecting an expression`

### 5. Semantics Stay the Same

`BlockExprAST::codegen()` is unchanged conceptually:

- evaluate prefix expressions in order
- return the value of final `return` expression

So Chapter 11 is mostly a **syntax/lexing** upgrade, not a semantic one.

## Compile and Test

```bash
cd code/chapter09
./build.sh
llvm-lit test/
```

## Sample Session

```bash
$ ./build/pyxc repl
ready> def print_sequence(x):
ready>   x + 100
ready>   return x + 2
ready> print_sequence(3)
5.000000
```

And with blank lines/comments in a block:

```py
def plus5(x):
  # prefix statement
  x + 100

  return x + 5
```

## Error Cases

Missing indented block:

```py
def bad(x):
return x + 1
```

Produces:

- `Expected indented block after ':'`

Inconsistent indentation:

```py
def bad(x):
   x + 1
  return x + 2
```

Produces:

- `inconsistent indentation`

No return in function body:

```py
def bad2(x):
  x + 1
  x + 2
```

Produces:

- `Expected at least one return statement in function body`

## Testing Your Implementation

This chapter includes **72 automated tests**. Run them with:

```bash
cd code/chapter09
llvm-lit -v test/
# or: lit -v test/
```

**Pro tip:** Key tests include:

- `block_semicolon_return.pyxc` - Basic indentation block with prefix + return
- `block_multiple_expr_statements.pyxc` - Multiple prefix statements before return
- `block_indent_comments_blanklines.pyxc` - Comments/blank lines inside blocks
- `error_missing_indented_block.pyxc` - Missing required indentation after `:`
- `error_inconsistent_indentation.pyxc` - Non-matching dedent levels
- `error_block_no_return.pyxc` - Block must contain a return

Browse the tests to see exactly what behavior is supported.

## What We Built

- Lexer-level indentation transformation (`INDENT`/`DEDENT`)
- Newline-delimited statement parsing for function blocks
- Strict indentation diagnostics
- Same block semantics as Chapter 10, now with cleaner syntax
- Fully passing chapter 9 test suite

## What's Next

With block syntax now Python-like, the next natural step is **mutable variables** (`let` + assignment), then control flow (conditionals/loops).

## Need Help?

Stuck? Questions? Found a bug?

- Issues: [GitHub Issues](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- Discussions: [GitHub Discussions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:

- Chapter number (`14`)
- Your platform
- Command you ran
- Full error output
