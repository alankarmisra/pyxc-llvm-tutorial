---
description: "Add &&, ||, and ! — logical operators with short-circuit evaluation and a dedicated bool result type."
---
# 33. pyxc: Logical Operators

## Where We Are

[Chapter 32](chapter-32.md) completed arithmetic — division, remainder, compound assignment, and `++`/`--`. Conditions in `if` and `while` can now involve complex expressions, but there is still no way to combine two boolean checks or negate one. After this chapter:

```pyxc
extern def printd(x: float64)

def is_between(x: int, lo: int, hi: int) -> bool:
  return x >= lo && x <= hi

def main() -> int:
  var a: bool = True
  var b: bool = !a
  if b || is_between(5, 1, 10):
    printd(1.0)
  return 0
```

```
1.000000
```

`&&` and `||` short-circuit: the right-hand side is not evaluated if the result is already determined by the left.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-33
```

## New Tokens and Lexer Peek-Ahead

Two new token values:

```cpp
tok_and = -50, // &&
tok_or  = -51, // ||
```

Single `&` and `|` remain as their ASCII character values — they are distinct tokens (bitwise operators, added in a later chapter). The lexer peeks one character ahead to decide which to emit:

```cpp
if (LexerLastChar == '&') {
  int Tok = (peek() == '&') ? (advance(), tok_and) : '&';
  LexerLastChar = advance();
  return Tok;
}

if (LexerLastChar == '|') {
  int Tok = (peek() == '|') ? (advance(), tok_or) : '|';
  LexerLastChar = advance();
  return Tok;
}
```

If the next character is another `&` or `|`, `advance()` consumes it and the two-character token is returned. Otherwise the single-character token falls through unchanged.

## Precedence

`||` and `&&` get their own entries in the precedence table, sitting below all arithmetic and comparison operators:

```cpp
{tok_or,  5},  // ||
{tok_and, 7},  // &&
```

The full ordering from lowest to highest: `||` (5) → `&&` (7) → comparisons (10) → arithmetic (20–40). `&&` binds more tightly than `||`, so `a || b && c` parses as `a || (b && c)`.

## Type Checking — `IsLogicalOp` and `GetBinaryResultType`

A new predicate identifies the logical operators:

```cpp
static bool IsLogicalOp(int Op) { return Op == tok_and || Op == tok_or; }
```

`GetBinaryResultType` gains a branch for them. Both operands must be `bool`; anything else is a type error:

```cpp
if (IsLogicalOp(Op)) {
  if (L == ValueType::Bool && R == ValueType::Bool)
    return ValueType::Bool;
  return ValueType::Error;
}
```

The result type is always `bool`. In `ParseBinOpRHS`, the check for built-in operators is extended:

```cpp
if (IsComparisonOp(BinOp) || IsArithmeticOp(BinOp) || IsLogicalOp(BinOp)) {
  ResultType = GetBinaryResultType(BinOp, LHS->getType(), ...);
  if (ResultType == ValueType::Error)
    return LogError("Type mismatch in binary operator");
  ...
}
```

## `LogicalNotExprAST` — Built-In `!` for Bool

`!` gets a dedicated AST node separate from user-defined unary operators:

```cpp
class LogicalNotExprAST : public ExprAST {
  unique_ptr<ExprAST> Operand;
public:
  explicit LogicalNotExprAST(unique_ptr<ExprAST> Operand)
      : Operand(std::move(Operand)) {
    setType(ValueType::Bool);
  }
  Value *codegen() override;
};
```

The constructor immediately sets the result type to `Bool` — no type inference needed.

Parsing `!` in `ParseUnary` checks whether the operand is `bool`. If so, it creates `LogicalNotExprAST`. If not, it falls through to the user-defined `unary!` lookup for backward compatibility:

```cpp
if (CurTok == '!') {
  getNextToken(); // eat '!'
  auto Operand = ParseUnary();
  if (!Operand)
    return nullptr;
  if (Operand->getType() == ValueType::Bool)
    return make_unique<LogicalNotExprAST>(std::move(Operand));
  auto Proto = GetFunctionProto("unary!");
  if (!Proto)
    return LogError("Unknown unary operator");
  if (Proto->getNumArgs() != 1)
    return LogError("Unary operator must have exactly one argument");
  ValueType ParamType = Proto->getArgType(0);
  if (!IsAssignable(ParamType, Operand->getType())) {
    return LogError(
        ("unary operator expects " + string(TypeName(ParamType))).c_str());
  }
  return make_unique<UnaryExprAST>('!', std::move(Operand),
                                   Proto->getReturnType());
}
```

Codegen for `LogicalNotExprAST` emits a single `CreateNot` on the `i1` value:

```cpp
Value *LogicalNotExprAST::codegen() {
  Value *V = Operand->codegen();
  if (!V)
    return nullptr;
  if (Operand->getType() != ValueType::Bool)
    return LogErrorV("Type mismatch in unary operator");
  return Builder->CreateNot(V, "nottmp");
}
```

## Short-Circuit Codegen for `&&` and `||`

`&&` and `||` do not use the standard binary expression path. In `BinaryExprAST::codegen`, they are intercepted before the operand is evaluated on the right:

```cpp
if (Op == tok_and || Op == tok_or) {
  Value *L = LHS->codegen();
  if (!L)
    return nullptr;
  if (LHS->getType() != ValueType::Bool || RHS->getType() != ValueType::Bool)
    return LogErrorV("Type mismatch in binary operator");

  Function *F = Builder->GetInsertBlock()->getParent();
  BasicBlock *LHSBB  = Builder->GetInsertBlock();
  BasicBlock *RHSBB  = BasicBlock::Create(*TheContext, "logic.rhs", F);
  BasicBlock *MergeBB = BasicBlock::Create(*TheContext, "logic.end");

  if (Op == tok_and)
    Builder->CreateCondBr(L, RHSBB, MergeBB);
  else
    Builder->CreateCondBr(L, MergeBB, RHSBB);

  Builder->SetInsertPoint(RHSBB);
  Value *RHSVal = RHS->codegen();
  if (!RHSVal)
    return nullptr;
  Builder->CreateBr(MergeBB);
  RHSBB = Builder->GetInsertBlock();

  F->insert(F->end(), MergeBB);
  Builder->SetInsertPoint(MergeBB);
  PHINode *PN =
      Builder->CreatePHI(Type::getInt1Ty(*TheContext), 2, "logictmp");
  if (Op == tok_and) {
    PN->addIncoming(ConstantInt::getFalse(*TheContext), LHSBB);
    PN->addIncoming(RHSVal, RHSBB);
  } else {
    PN->addIncoming(ConstantInt::getTrue(*TheContext), LHSBB);
    PN->addIncoming(RHSVal, RHSBB);
  }
  return PN;
}
```

For `a && b`:
- Evaluate `a`.  If false, jump to `logic.end` with a `false` constant.
- If true, fall into `logic.rhs`, evaluate `b`, jump to `logic.end`.
- The `phi` node in `logic.end` selects between `false` (the short-circuit path) and the result of `b`.

For `a || b` the condition is inverted: if `a` is true, jump to `logic.end` with `true` immediately. The PHI node produces `true` on that path and the result of `b` on the other.

The LLVM `phi` uses `i1` — the native boolean type — throughout. If `b` is a function call with side effects, it genuinely does not execute when the short-circuit fires.

## Grammar

`!` is added to `unaryop`. `&&` and `||` join `builtinbinaryop`.

```ebnf
unaryop         = "-" | "!" | "++" | "--" | userdefunaryop ;  -- changed
builtinbinaryop = "+" | "-" | "*" | "/" | "%"
                | "<" | "<=" | ">" | ">=" | "==" | "!="
                | "&&" | "||" ;                               -- changed
```

## Error Cases

**Non-bool operand:**
```pyxc
var x: int = 1
var y: bool = True
if x && y:  # Error: Type mismatch in binary operator
  return 1
```

Both sides must be `bool`. There is no implicit conversion from `int` to `bool`.

## Things Worth Knowing

**Short-circuit is real, not just an optimisation.** The right-hand side is structurally placed behind a conditional branch in the IR. A function call on the right of `&&` or `||` will genuinely not execute if the left determines the result.

**`!` on a non-bool falls through to user-defined `unary!`.** If you have defined a custom `unary!` for some other type, it continues to work. The built-in path only activates for `bool`.

**`&&` and `||` are not bitwise.** For bitwise AND and OR on integers, see [Chapter 35](chapter-35.md).

## What's Next

[Chapter 34](chapter-34.md) adds `while`, `do/while`, `break`, and `continue`.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.
