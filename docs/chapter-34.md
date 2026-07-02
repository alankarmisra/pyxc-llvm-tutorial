---
description: "Add bitwise operators &, |, ^, <<, >> and unary ~ with C-standard precedence and integer-only type checking."
---
# 34. pyxc: Bitwise Operators

## Where We Are

[Chapter 33](chapter-33.md) completed the loop story. The last major gap before K&R-style systems programming is bitwise manipulation. After this chapter, flags, masks, and bit-shifting all work:

```pyxc
extern def printd(x: float64)

def main() -> int:
  var flags: int = 0
  flags = flags | 1        # set bit 0
  flags = flags | 4        # set bit 2
  flags = flags & ~2       # clear bit 1 (already clear, but pattern works)

  var shifted: int = 1 << 3   # 8
  var masked: int = shifted & 0xFF

  printd(float64(flags + masked))
  return 0
```

```
13.000000
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-34
```

## New Tokens for `<<` and `>>`

Single-character operators `&`, `|`, `^`, and `~` are already handled by the catch-all ASCII path — they are returned as their character values. The shift operators require two new token values because `<` and `>` are already comparison operators:

```cpp
tok_shl = -58, // <<
tok_shr = -59, // >>
```

## Lexer Peek-Ahead for Shifts

The existing `<` and `>` paths in the lexer previously looked ahead for `=` only. They are expanded to also look for a second `<` or `>`:

```cpp
if (LexerLastChar == '<') {
  int Next = peek();
  int Tok = '<';
  if (Next == '=')
    Tok = (advance(), tok_leq);   // '<=' — comparison
  else if (Next == '<')
    Tok = (advance(), tok_shl);   // '<<' — left shift
  LexerLastChar = advance();
  return Tok;
}

if (LexerLastChar == '>') {
  int Next = peek();
  int Tok = '>';
  if (Next == '=')
    Tok = (advance(), tok_geq);   // '>=' — comparison
  else if (Next == '>')
    Tok = (advance(), tok_shr);   // '>>' — right shift
  LexerLastChar = advance();
  return Tok;
}
```

`peek()` reads ahead without consuming. If the next character matches, `advance()` consumes it and the two-character token is returned. Any other character leaves `Tok` as the single character.

## Precedence — A Full Restructuring

Adding bitwise operators requires reorganising the existing precedence table. Previously all comparisons sat at the same level (10). The new table follows C exactly:

```cpp
{tok_or,  5},  // ||
{tok_and, 7},  // &&
{'|',    10},  // |
{'^',    11},  // ^
{'&',    12},  // &
{tok_eq, 13},  // ==
{tok_neq,13},  // !=
{tok_leq,14},  // <=
{tok_geq,14},  // >=
{'<',    14},  // <
{'>',    14},  // >
{tok_shl,15},  // <<
{tok_shr,15},  // >>
// arithmetic remains at 20–40
```

The important consequence: `&` is below `==` and `!=`. So `a & b == 0` parses as `a & (b == 0)`, not `(a & b) == 0`. If you want the latter, add parentheses.

## Type-Checking Predicates and `GetBinaryResultType`

Two predicates identify the new operator families:

```cpp
static bool IsBitwiseOp(int Op) { return Op == '&' || Op == '|' || Op == '^'; }
static bool IsShiftOp(int Op)   { return Op == tok_shl || Op == tok_shr; }
```

`GetBinaryResultType` gains two new branches. For bitwise ops, both operands must be integers; `IsAssignable` picks the wider of the two as the result type:

```cpp
if (IsBitwiseOp(Op)) {
  if (!IsIntType(L) || !IsIntType(R))
    return ValueType::Error;
  if (IsAssignable(L, R))
    return L;
  if (IsAssignable(R, L))
    return R;
  return ValueType::Error;
}
```

For shifts, the result type is always the left operand's type, regardless of the shift count's type:

```cpp
if (IsShiftOp(Op)) {
  if (!IsIntType(L) || !IsIntType(R))
    return ValueType::Error;
  return L;
}
```

In `ParseBinOpRHS`, the built-in type-check dispatch is extended:

```cpp
if (IsComparisonOp(BinOp) || IsArithmeticOp(BinOp) || IsLogicalOp(BinOp) ||
    IsBitwiseOp(BinOp) || IsShiftOp(BinOp)) {
  ResultType = GetBinaryResultType(BinOp, LHS->getType(), ...);
  if (ResultType == ValueType::Error)
    return LogError("Type mismatch in binary operator");
  LHS = make_unique<BinaryExprAST>(...);
  continue;
}
```

Type errors are caught at parse time. Codegen never runs on a bad operand pair.

## Parsing Unary `~`

`~` is parsed in `ParseUnary` alongside `-` and `!`. The operand must be an integer type; the result type is the same as the operand:

```cpp
if (CurTok == '~') {
  getNextToken(); // eat '~'
  auto Operand = ParseUnary();
  if (!Operand)
    return nullptr;
  if (!IsIntType(Operand->getType()))
    return LogError("Unary '~' requires an integer operand");
  return make_unique<UnaryExprAST>('~', std::move(Operand),
                                   Operand->getType());
}
```

`~~x` (double complement) and `~(x + 1)` both parse naturally because the operand is a full `unaryexpr`.

## Codegen: Binary Bitwise Operators

`BinaryExprAST::codegen` gains cases for `&`, `|`, and `^`. Both operands are coerced to the result type via `EmitImplicitCast` — this handles widening selected by `GetBinaryResultType` (e.g., `int32 & int64` widens `int32` to `int64` before the instruction):

```cpp
case '&':
case '|':
case '^': {
  ValueType Ty = getType();
  L = EmitImplicitCast(L, LType, Ty);
  R = EmitImplicitCast(R, RType, Ty);
  if (!L || !R)
    return LogErrorV("Type mismatch in binary operator");
  if (Op == '&')
    return Builder->CreateAnd(L, R, "bwand");
  if (Op == '|')
    return Builder->CreateOr(L, R, "bwor");
  return Builder->CreateXor(L, R, "bwxor");
}
```

Each maps directly to a single LLVM instruction: `and`, `or`, or `xor`. These are integer instructions — LLVM has no floating-point equivalents.

Shifts follow the same structure:

```cpp
case tok_shl:
case tok_shr: {
  ValueType Ty = getType();
  L = EmitImplicitCast(L, LType, Ty);
  R = EmitImplicitCast(R, RType, Ty);
  if (!L || !R)
    return LogErrorV("Type mismatch in binary operator");
  if (Op == tok_shl)
    return Builder->CreateShl(L, R, "shltmp");
  return Builder->CreateAShr(L, R, "shrtmp");
}
```

`CreateShl` emits `shl` — shift left, filling low bits with zeros. `CreateAShr` emits `ashr` — arithmetic shift right, which fills high bits with the sign bit of the left operand. For a negative `int64`, `x >> 1` stays negative. LLVM also has `CreateLShr` for logical (zero-filling) right shift, but pyxc doesn't expose it — `ashr` is correct for signed integers.

## Codegen: Unary `~`

`UnaryExprAST::codegen` handles `~` after existing cases for `-` and `!`:

```cpp
if (Opcode == '~') {
  if (!IsIntType(getType()))
    return LogErrorV("Unary '~' not supported for this type");
  return Builder->CreateNot(Op, "bnottmp");
}
```

`CreateNot` lowers to `xor %val, -1` — XOR-ing every bit with 1 flips it. The name `bnottmp` (bitwise not) distinguishes it from `nottmp` (logical not on `i1`) in the IR.

For a concrete example:
```pyxc
var x: int = 9     # binary: ...0001001
var y: int = ~x    # binary: ...1110110 → -10 in two's complement
var z: int = y & 7 # mask the low 3 bits → 6
```

`~9` is `-10` because in two's complement, flipping all bits and adding 1 negates: `~x = -(x + 1)`.

## Grammar

```ebnf
unaryop         = "-" | "!" | "~" | "++" | "--" | userdefunaryop ;  -- changed
builtinbinaryop = "+" | "-" | "*" | "/" | "%"
                | "<" | "<=" | ">" | ">=" | "==" | "!="
                | "&&" | "||"
                | "&" | "|" | "^" | "<<" | ">>" ;                  -- changed
```

## Error Cases

**Bitwise op on float:**
```pyxc
var x: float64 = 1.0
var y: float64 = 2.0
var z: float64 = x & y  # Error: Type mismatch in binary operator
```

**`~` on non-integer:**
```pyxc
var x: float64 = 1.0
var y: float64 = ~x  # Error: Unary '~' requires an integer operand
```

Both are caught at parse time and never reach codegen.

## Things Worth Knowing

**`&` and `|` are not `&&` and `||`.** The single-character forms are bitwise and operate on integers. The double-character forms are logical, operate on `bool`, and short-circuit.

**Compound assignment works with all bitwise operators.** `x &= mask`, `flags |= bit`, `x ^= pattern`, `x <<= 2`, and `x >>= 1` are all valid — the compound assignment path already handles any binary operator by token value.

**Right shift is arithmetic (sign-extending).** For a negative `int`, `x >> 1` fills the high bit with the sign bit. Chapter 38 adds unsigned integer types, which use logical shift right (`lshr`).

**The C precedence gotcha.** `a & b == 0` means `a & (b == 0)` in both C and pyxc. Write `(a & b) == 0` when you want that.

## What's Next

[Chapter 35](chapter-35.md) adds `switch` statements.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.
