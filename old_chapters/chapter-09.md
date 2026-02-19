---
description: "Add comparison operators (`==`, `!=`, `<=`, `>=`) to the lexer, parser, and LLVM IR codegen."
---
# 9. Pyxc: Comparison Operators

## What We're Building

In Chapter 6, Pyxc supported arithmetic operators and basic comparisons (`<`, `>`).  
In this chapter, we add the missing comparison set:

- `==`
- `!=`
- `<=`
- `>=`

These operators are essential for real control flow in upcoming chapters (`if`, `while`, and block-based logic).

At the end of this chapter:

- The lexer recognizes multi-character comparison tokens.
- The parser handles them with the same precedence tier as `<` and `>`.
- Codegen emits the correct LLVM floating-point comparisons.
- Existing behavior stays intact (`+ - * / % < >`).

## Grammar Change (EBNF)

Before touching code, it helps to define the language shape clearly.  
For Chapter 9, the expression grammar becomes:

```ebnf
expression     = comparison ;
comparison     = additive { compareop additive } ;
additive       = multiplicative { ("+" | "-") multiplicative } ;
multiplicative = primary { ("*" | "/" | "%") primary } ;
compareop      = "<" | ">" | "<=" | ">=" | "==" | "!=" ;
```

This matches the parser flow we already use:

- `ParseExpression()` starts expression parsing
- `ParseBinOpRHS()` handles operator-precedence chaining
- `ParsePrimary()` handles leaf forms (identifier/number/paren)

So we are extending existing parser structure, not introducing a brand-new parse model.

## Source Code

Grab the code: [code/chapter07](https://github.com/alankarmisra/pyxc-llvm-tutorial/tree/main/code/chapter07)

Or clone the whole repo:

```bash
git clone https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter07
```

## Why This Chapter Matters

Arithmetic answers "what number is this?"  
Comparisons answer "is this condition true?"

Pyxc currently models booleans as `double` values (`0.0` false, `1.0` true), so comparisons become a bridge between numeric expressions and condition-style logic.

## Implementation

### 1. Add New Tokens

Extend the token enum with multi-character operators:

```cpp
// multi-char comparison operators
tok_eq = -9,  // ==
tok_ne = -10, // !=
tok_le = -11, // <=
tok_ge = -12  // >=
```

Also add debug names so diagnostics and token dumps stay readable:

```cpp
{tok_eq, "'=='"},
{tok_ne, "'!='"},
{tok_le, "'<='"},
{tok_ge, "'>='"},
```

### 2. Teach the Lexer Multi-Character Operators

Single-character tokens (`<`, `>`, etc.) were already supported.  
Now we need lookahead so we can distinguish:

- `<` vs `<=`
- `>` vs `>=`
- `=` vs `==`
- `!` vs `!=`

Implementation pattern:

```cpp
if (LastChar == '=') {
  int NextChar = advance();
  if (NextChar == '=') {
    LastChar = advance();
    return tok_eq;
  }
  LastChar = NextChar;
  return '=';
}
```

We do the same for `!`, `<`, and `>`.

Why return `'='` or `'!'` when the second character isn't present?  
Because parser-level syntax checks should still produce "Unexpected '='" / "Unexpected '!'" diagnostics, which is clearer than silently swallowing input.

### 3. Update Binary Operator Representation

`BinaryExprAST` used `char Op`, which only works for single-byte operators.  
Multi-character operators use enum tokens (negative integers), so change to:

```cpp
int Op;
```

and update the constructor accordingly.

### 4. Update Precedence Table

The old precedence map was keyed by `char`.  
Change it to `int` and include new comparison tokens at the same precedence as `<` and `>`:

```cpp
static std::map<int, int> BinopPrecedence = {
    {'<', 10}, {'>', 10}, {tok_le, 10}, {tok_ge, 10}, {tok_eq, 10},
    {tok_ne, 10}, {'+', 20}, {'-', 20}, {'*', 40}, {'/', 40},
    {'%', 40}};
```

### 5. Fix `GetTokPrecedence()` for Non-ASCII Tokens

The old code gated on `isascii(CurTok)`, which rejects enum tokens like `tok_eq`.  
Use direct map lookup instead:

```cpp
static int GetTokPrecedence() {
  auto It = BinopPrecedence.find(CurTok);
  if (It == BinopPrecedence.end())
    return NO_OP_PREC;
  int TokPrec = It->second;
  if (TokPrec < MIN_BINOP_PREC)
    return NO_OP_PREC;
  return TokPrec;
}
```

This is the key parser fix that makes `==`, `!=`, `<=`, `>=` parse as binary operators.

### 6. Add LLVM IR Codegen Cases

Add new branches in `BinaryExprAST::codegen()`:

```cpp
case tok_le:
  L = Builder->CreateFCmpULE(L, R, "cmptmp");
  return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
case tok_ge:
  L = Builder->CreateFCmpUGE(L, R, "cmptmp");
  return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
case tok_eq:
  L = Builder->CreateFCmpUEQ(L, R, "cmptmp");
  return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
case tok_ne:
  L = Builder->CreateFCmpUNE(L, R, "cmptmp");
  return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
```

Like existing `<` / `>` code, each comparison:

1. Produces `i1` with `fcmp`
2. Converts to `double` (`0.0` or `1.0`) using `uitofp`

## Compile and Test

```bash
cd code/chapter07
./build.sh
llvm-lit test/
```

## Sample Session

```bash
$ ./build/pyxc repl --emit=llvm-ir
ready> def test_eq(x, y): return x == y
define double @test_eq(double %x, double %y) {
entry:
  %cmptmp = fcmp ueq double %x, %y
  %booltmp = uitofp i1 %cmptmp to double
  ret double %booltmp
}

ready> def test_ge(x, y): return x >= y
define double @test_ge(double %x, double %y) {
entry:
  %cmptmp = fcmp uge double %x, %y
  %booltmp = uitofp i1 %cmptmp to double
  ret double %booltmp
}
```

Parse error examples remain clear:

```bash
ready> def bad(x,y): return x = y
Error (Line: 3, Column: 24): Unexpected '='
def bad(x,y): return x = 
                       ^~~~
                       
ready> def bad2(x,y): return x ! y
Error (Line: 4, Column: 25): Unexpected '!'
def bad2(x,y): return x ! 
                        ^~~~
```

## Testing Your Implementation

This chapter includes **58 automated tests**. Run them with:

```bash
cd code/chapter07
llvm-lit -v test/
# or: lit -v test/
```

**Pro tip:** Key tests include:

- `ir_operator_eq.pyxc` - Verifies `==` lowers to `fcmp ueq`
- `ir_operator_ne.pyxc` - Verifies `!=` lowers to `fcmp une`
- `ir_operator_le.pyxc` - Verifies `<=` lowers to `fcmp ule`
- `ir_operator_ge.pyxc` - Verifies `>=` lowers to `fcmp uge`
- `ir_comparison_precedence.pyxc` - Verifies comparison precedence integration with arithmetic
- `error_bare_equal.pyxc` - Verifies clear error for bare `=`
- `error_bare_bang.pyxc` - Verifies clear error for bare `!`

Browse the tests to see exactly what behavior is supported.

## What We Built

- Multi-character comparison tokens in the lexer
- Parser support for `== != <= >=`
- Correct precedence integration with existing arithmetic
- LLVM IR generation for all comparisons
- Comprehensive lit coverage for success and error cases

## What's Next

Now we have richer boolean-producing expressions.  
Next chapter can focus on block structure (explicit statement boundaries first), then indentation-based blocks as a lexical transformation.

## Need Help?

Stuck? Questions? Found a bug?

- Issues: [GitHub Issues](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- Discussions: [GitHub Discussions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:

- Chapter number (`12`)
- Your platform
- Command you ran
- Full error output
