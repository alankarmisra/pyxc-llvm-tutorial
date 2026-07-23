---
description: "Allow assignment inside an expression so patterns like while (c = getchar()) != EOF work without a separate priming read."
---
# 40. pyxc: Assignment as Expression

## Where We Are

[Chapter 39](chapter-39.md) added unsigned integer types. pyxc can call `getchar()`, but the canonical K&R idiom for reading until EOF still doesn't compile:

```pyxc
# What we want to write:
while (c = getchar()) != EOF:
    ...
```

```
Error: Assignment target must be assignable
```

The problem is that `=` is a statement in pyxc — it cannot appear inside an expression like a while condition. After this chapter it can:

```pyxc
extern def getchar() -> int32
extern def printd(x: float64)

var EOF: int32 = -1

def main() -> int:
  var c: int32
  var blanks: int
  while (c = getchar()) != EOF:
    if c == ' ':
      blanks += 1
  printd(float64(blanks))
  return 0
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-40
```

## `ParseExpression` — Tail Assignment Check

`ParseExpression` already parsed `unaryexpr binoprhs`. The change adds a check after `ParseBinOpRHS` resolves: if the next token is `=` or a compound-assign operator, treat the result as an lvalue and parse the right-hand side recursively:

```cpp
static unique_ptr<ExprAST> ParseExpression() {
  auto LHS = ParseUnary();
  if (!LHS)
    return nullptr;

  LHS = ParseBinOpRHS(0, std::move(LHS));
  if (!LHS)
    return nullptr;

  // Assignment tail — fires only if '=' or '+=' etc. follows
  if (CurTok != '=' && !IsCompoundAssignTok(CurTok))
    return LHS;

  int AssignTok = CurTok;
  getNextToken(); // eat assignment operator
  ExpectedLiteralTypeGuard Guard(LHS->getType(), LHS->getStructName());
  auto RHS = ParseExpression(); // right-associative: recurse
  if (!RHS)
    return nullptr;
  return BuildAssignmentExpr(AssignTok, std::move(LHS), std::move(RHS));
}
```

Because the right-hand side recurses to `ParseExpression`, chained assignment is automatically right-associative: `a = b = 4` parses as `a = (b = 4)`.

`ExpectedLiteralTypeGuard` propagates the lvalue's type into the RHS parse, so `x = 5` when `x` is `int32` will treat `5` as `int32` without an explicit cast.

## `BuildAssignmentExpr` — Lvalue Validation and Node Construction

All lvalue validation is done in `BuildAssignmentExpr`, a new helper extracted from the old statement-level assignment parser. It pattern-matches on the node type of `LHS`:

```cpp
static unique_ptr<ExprAST> BuildAssignmentExpr(int AssignTok,
                                               unique_ptr<ExprAST> LHS,
                                               unique_ptr<ExprAST> RHS) {
  if (!LHS || !RHS)
    return nullptr;

  if (auto *Var = dynamic_cast<VariableExprAST *>(LHS.get())) {
    // plain variable: produce AssignmentExprAST or CompoundAssignmentExprAST
    ...
  }
  if (auto *Field = dynamic_cast<FieldExprAST *>(LHS.get())) {
    // field access: produce FieldAssignmentExprAST or FieldCompoundAssignmentExprAST
    ...
  }
  if (auto *Idx = dynamic_cast<IndexExprAST *>(LHS.get())) {
    // array index: produce IndexAssignmentExprAST or IndexCompoundAssignmentExprAST
    ...
  }
  if (auto *IdxField = dynamic_cast<IndexedFieldExprAST *>(LHS.get())) {
    // indexed field: produce IndexedFieldAssignmentExprAST or variant
    ...
  }

  return LogError("Assignment target must be assignable");
}
```

Four lvalue kinds are recognised: variable, field, array index, and indexed field. Any other expression kind — including `x + 1` or a function call — reaches the final `LogError`. The existing `AssignmentExprAST`, `CompoundAssignmentExprAST`, and their field/index variants are reused unchanged; `BuildAssignmentExpr` is the only new code.

## The Value of an Assignment Expression

`AssignmentExprAST::codegen` already stores the value and returns it. That return value is now visible to the caller because `ParseExpression` can produce an assignment node at any nesting level:

```pyxc
var result: int = (c = 5) + 1   # result is 6; c is 5
```

Compound assignment also works as an expression. `(n -= 1)` produces the new value of `n`.

## Right Associativity

The recursive call to `ParseExpression` (not `ParseBinOpRHS`) means `=` binds more loosely than all binary operators and chains right:

```pyxc
a = b = 4   # parsed as: a = (b = 4)
```

`b = 4` evaluates first — stores 4 into `b`, produces 4 — then `a = 4` stores that value into `a`.

## Parentheses Are Transparent for Lvalues

The parens in `(c = getchar())` are parsed as a `ParenExprAST` that wraps a `VariableExprAST`. `BuildAssignmentExpr` sees through the paren by the time it runs because `ParsePrimary` returns the inner expression node from `ParseParenExpr`. So assignment through `(x)` works correctly:

```pyxc
(x) = 2     # valid: same as x = 2
(p[i]) = v  # valid: same as p[i] = v
```

## Grammar

```ebnf
expression = lvalue assignop expression | unaryexpr binoprhs ; -- changed
```

The `assignstmt` rule is unchanged — assignment as a statement still works as before.

## Error Cases

**Non-lvalue target:**
```pyxc
(x + 1) = 3   # Error: Assignment target must be assignable
```

**Precedence trap without parens:**
```pyxc
while c = getchar() != EOF:
  ...
# Parses as: c = (getchar() != EOF)
# If c is int32, this is a type error (assigning bool to int32).
# Always use: while (c = getchar()) != EOF:
```

## Things Worth Knowing

**Parentheses required when combining assignment with another operator.** `(c = getchar()) != EOF` is correct. `c = getchar() != EOF` assigns a `bool` to `c`.

**Assignment expressions do not print in the REPL.** Top-level assignments set variables silently, exactly as before.

**Compound assignment also works as an expression.** `(n -= 1)` produces the new value of `n`. Idioms like `while (n -= 1) > 0:` work.

## What's Next

[Chapter 41](chapter-41.md) adds variadic `extern` declarations, enabling `printf`, `scanf`, and other C variadic functions.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.
