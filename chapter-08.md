---
description: "Add semicolon-terminated blocks so function bodies can contain multiple statements before returning a value."
---
# 8. Pyxc: Blocks with `;`

## What We're Building

In Chapter 7, function bodies were effectively a single expression after `return`:

```python
def add(x, y):
  return x + y
```

That works, but it prevents multi-step logic inside a function body.

In this chapter, we add **explicit statement blocks** using `;` as a separator.

Now this is valid:

```python
extern def printd(x)
extern def printlf()

def print_sequence(x):
  printd(x);
  printlf();
  printd(x + 1);
  printlf();
  return x + 2
```

The first expression is evaluated for side effects, and the `return` expression provides the function value.

This chapter is intentionally separate from indentation-based blocks. We focus only on block semantics first.

## Source Code

Grab the code: [code/chapter08](https://github.com/alankarmisra/pyxc-llvm-tutorial/tree/main/code/chapter08)

Or clone the whole repo:

```bash
git clone https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter08
```

## Grammar Change (EBNF)

Before coding, define the new shape clearly:

```ebnf
definition = "def" prototype ":" block ;
block      = statement { ";" statement } [ ";" ] ;
statement  = "return" expression
           | expression ;
```

This means:

- A function body is now a **block**, not a single expression.
- A block is one or more statements separated by `;`.
- A trailing `;` is allowed.
- Newlines are allowed after `;`, so each statement can sit on its own line.
- A statement is either:
  - a plain expression, or
  - `return expression`

## Why This Design

We could jump straight to indentation-driven blocks, but that mixes two problems:

1. **Block semantics** (what a multi-statement body means)
2. **Block syntax** (how we detect boundaries)

Using `;` first isolates semantics. Later, indentation can become a lexical transformation over already-working block behavior.

## Implementation

### 1. Update Definition Parsing

Old rule (chapter 7):

- `def ... : return expression`

New rule (chapter 8):

- `def ... : block`

So `ParseDefinition()` now calls `ParseBlockExpr()` after parsing `:` and optional newlines.

### 2. Add a Block AST Node

We introduce `BlockExprAST`:

```cpp
class BlockExprAST : public ExprAST {
  std::vector<std::unique_ptr<ExprAST>> PrefixExprs;
  std::unique_ptr<ExprAST> ReturnExpr;

public:
  BlockExprAST(std::vector<std::unique_ptr<ExprAST>> PrefixExprs,
               std::unique_ptr<ExprAST> ReturnExpr)
      : PrefixExprs(std::move(PrefixExprs)), ReturnExpr(std::move(ReturnExpr)) {}
  Value *codegen() override;
};
```

Model:

- `PrefixExprs` = statements before `return`
- `ReturnExpr` = final returned expression

### 3. Parse Semicolon-Separated Statements

Here is the parser entry point:

```cpp
static std::unique_ptr<ExprAST> ParseBlockExpr() {
  std::vector<std::unique_ptr<ExprAST>> PrefixExprs;
  ...
}
```

It builds exactly what `BlockExprAST` needs:

- `PrefixExprs` for non-return statements
- one required `ReturnExpr` at the end

#### 3.1 Skip leading newlines in the block

```cpp
while (CurTok == tok_eol)
  getNextToken();
```

This lets users write:

```py
def foo(x):

  x + 1;
  return x + 2
```

without treating the blank line as a syntax error.

#### 3.2 Parse prefix statements until `return`

```cpp
while (CurTok != tok_return) {
  if (CurTok == tok_eof)
    return LogError<ExprPtr>(
        "Expected at least one return statement in function body");

  auto E = ParseExpression();
  if (!E)
    return nullptr;
  PrefixExprs.push_back(std::move(E));
  ...
}
```

Every non-return statement is parsed as a normal expression and stored.

#### 3.3 Enforce `;` separators

```cpp
if (CurTok == ';') {
  getNextToken(); // consume ';'
  while (CurTok == tok_eol)
    getNextToken(); // allow newline-separated statements
  continue;
}
```

This is the core rule: if a statement is followed by another statement, there must be a semicolon between them.

If separator syntax is wrong, we emit:

```cpp
return LogError<ExprPtr>("Expected ';' after statement");
```

#### 3.4 Require at least one `return`

If we hit end-of-file while still parsing prefix statements, we emit:

```cpp
return LogError<ExprPtr>(
    "Expected at least one return statement in function body");
```

So blocks like this are rejected:

```py
def bad(x):
  x + 1;
  x + 2
```

#### 3.5 Parse final return expression

Once `tok_return` is found:

```cpp
getNextToken(); // consume 'return'
auto ReturnExpr = ParseExpression();
if (!ReturnExpr)
  return nullptr;

if (CurTok == ';')
  getNextToken(); // optional trailing semicolon

return std::make_unique<BlockExprAST>(std::move(PrefixExprs),
                                      std::move(ReturnExpr));
```

So this all works:

```py
x + 1;
return x + 2
```

and this also works:

```py
x + 1;
return x + 2;
```

### 4. Codegen for Block Expressions

`BlockExprAST::codegen()` is straightforward:

```cpp
Value *BlockExprAST::codegen() {
  for (auto &E : PrefixExprs) {
    if (!E->codegen())
      return nullptr;
  }
  return ReturnExpr->codegen();
}
```

Prefix expressions are evaluated in order. Their values are discarded.
The return expression value becomes the function return value.

## Compile and Test

```bash
cd code/chapter08
./build.sh
llvm-lit test/
```

## Sample Session

```bash
$ ./build/pyxc repl
ready> extern def printd(x)
ready> extern def printlf()
ready> def print_sequence(x):
ready>   printd(x);
ready>   printlf();
ready>   printd(x + 1);
ready>   printlf();
ready>   return x + 2
ready> print_sequence(3)
3.000000
4.000000
5.000000
```

Trailing semicolon also works:

```bash
ready> def ten(x):
ready>   x + 100;
ready>   return x + 3;
ready> ten(7)
10.000000
```

## More Useful Runtime Examples

Chapter 8 now includes a few practical runtime helpers:

- `printlf()` - print newline
- `flushd()` - flush stderr
- `seedrand(seed)` - seed RNG
- `randd(max)` - random value in `[0, max)`
- `clockms()` - current time in milliseconds

You can use them to write examples that feel less toy-like:

```py
extern def printd(x)
extern def printlf()
extern def flushd()
extern def seedrand(x)
extern def randd(max)
extern def clockms()

def report_tick(limit):
  seedrand(123);
  printd(clockms());
  printlf();
  printd(randd(limit));
  printlf();
  flushd();
  return randd(limit)
```

And a simple sequence helper:

```py
extern def printd(x)
extern def printlf()

def print_sequence(x):
  printd(x);
  printd(x + 1);
  printd(x + 2);
  printlf();
  return x + 3
```

## Error Cases

Missing separator:

```python
def bad(x):
  x + 1 return x + 2
```

Produces:

- `Expected ';' after statement`

No return in block:

```python
def bad2(x):
  x + 1;
  x + 2
```

Produces:

- `Expected at least one return statement in function body`

Empty statement (`;;`):

```python
def bad3(x):
  x + 1;;
  return x + 2
```

Produces:

- `Unexpected ';' when expecting an expression`

## Testing Your Implementation

This chapter includes **69 automated tests**. Run them with:

```bash
cd code/chapter08
llvm-lit -v test/
# or: lit -v test/
```

**Pro tip:** Key tests include:

- `block_semicolon_return.pyxc` - Basic multi-statement block support
- `block_semicolon_trailing.pyxc` - Optional trailing semicolon
- `block_multiple_expr_statements.pyxc` - Multiple prefix expressions before return
- `error_block_missing_separator.pyxc` - Missing `;` between statements
- `error_block_no_return.pyxc` - Block requires at least one return
- `error_block_empty_statement.pyxc` - Detects invalid empty statement (`;;`)
- `repl_runtime_print_helpers.pyxc` - Verifies `printlf()`/`flushd()` runtime helpers
- `repl_runtime_random.pyxc` - Verifies `seedrand()` and `randd()`
- `repl_runtime_clockms.pyxc` - Verifies `clockms()` runtime helper

Browse the tests to see exactly what behavior is supported.

## What We Built

- A real block grammar for function bodies
- `;`-separated statement parsing
- A dedicated block AST node (`BlockExprAST`)
- Deterministic error handling for malformed blocks
- Fully passing chapter 8 test suite

## What's Next

Now that block semantics are stable, we can replace explicit separators with indentation-based structure in the lexer (INDENT/DEDENT transformation) without changing core block meaning.

## Need Help?

Stuck? Questions? Found a bug?

- Issues: [GitHub Issues](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- Discussions: [GitHub Discussions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:

- Chapter number (`13`)
- Your platform
- Command you ran
- Full error output
