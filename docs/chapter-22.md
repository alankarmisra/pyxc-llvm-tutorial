---
description: "Add bitwise operators &, |, ^, <<, >> and unary ~ with C-standard precedence and integer-only type checking."
---
# 22. pyxc: Bitwise Operators

## What I Am Building

I'm pretty much done with the loop story after [Chapter 14](chapter-14.md). The last major gap before K&R-style systems programming is bitwise manipulation. If I add that, I can use flags, masks, and bit-shifting in my code and crack more of the K&R-style problems. Here's what I'm aiming to get working:

```pyxc
extern def printd(x: float64)

def main() -> int:
  var flags: int = 0
  flags = flags | 1        # set bit 0
  flags = flags | 4        # set bit 2
  flags = flags & ~2       # clear bit 1 (already clear, but pattern works)

  var shifted: int = 1 << 3   # 8
  var masked: int = shifted & 255

  printd(float64(flags + masked))
  return 0
```

```text
13.000000
```

I use `255` rather than `0xFF` here: pyxc doesn't have hexadecimal number literals yet, so `0xFF` doesn't parse. I confirmed this by trying it directly — it's not something this chapter adds either.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-22
```

## Grammar

`&`, `|`, `^`, `<<`, and `>>` each get their own grammar tier, following C's precedence ordering. The old flat `comparison` production splits into `equality` and `relational`, with `shift` and the three new bitwise tiers slotting in around them; `factor` gains `~`:

```grammardiff
*...
*expression                        = logical-or ;
*logical-or                        = logical-and { "||" logical-and } ;
-logical-and                       = comparison { "&&" comparison } ;
-comparison                        = sum { comparison-operator sum } ;
-comparison-operator               = "==" | "!=" | "<=" | ">=" | "<" | ">" ;
+logical-and                       = bitwise-or { "&&" bitwise-or } ;
+bitwise-or                        = bitwise-xor { "|" bitwise-xor } ;
+bitwise-xor                       = bitwise-and { "^" bitwise-and } ;
+bitwise-and                       = equality { "&" equality } ;
+equality                          = relational { ("==" | "!=") relational } ;
+relational                        = shift { ("<" | "<=" | ">" | ">=") shift } ;
+shift                             = sum { ("<<" | ">>") sum } ;
*sum                               = term { ("+" | "-") term } ;
*term                              = factor { ("*" | "/" | "%") factor } ;
*lvalue                            = name ;
*variable-binding                  = name ":" type [ "=" expression ] ;
-factor                            = ("-" | "!") factor | primary ;
+factor                            = ("-" | "!" | "~") factor | primary ;
*primary                           = cast-expression
*                                    | name-expression
*...
```

`bitwise-and` sits directly above `equality`, which is exactly what produces C's famous precedence gotcha: since each side of `&` is a full `equality` (which can itself contain `==`), `a & b == 0` parses as `a & (b == 0)`, not `(a & b) == 0`. I hit this myself while testing rather than just asserting it: `a & b == 0` for integer `a`, `b` is a real type error, "Type mismatch in binary operator", precisely because it parses as `a & (b == 0)` and `&` refuses a `bool` operand on the right. Getting `(a & b) == 0` requires the parentheses.

## New Tokens for `<<` and `>>`

Single-character operators like `&`, `|`, `^`, and `~` already fall through the lexer's catch-all ASCII path, returning their own character values as tokens. `<<` and `>>` are two-character, so they need real token values:

```cppdiff
*enum Token {
*  ...
*  tok_and = -41, // &&
*  tok_or = -42,  // ||
+  tok_shift_left = -43,  // <<
+  tok_shift_right = -44, // >>
*
*  // punctuation and operators
*  ...
*};
```

## Lexer Peek-Ahead for Shifts

The existing `<` and `>` paths already peeked one character ahead for `=` (to produce `<=`/`>=`). I extend them to also check for a second `<` or `>`:

```cpp
if (LexerLastChar == '<') {
  int Next = peek();
  int Tok = tok_less;
  if (Next == '=')
    Tok = (advance(), tok_leq);
  else if (Next == '<')
    Tok = (advance(), tok_shift_left);
  LexerLastChar = advance();
  return Tok;
}

if (LexerLastChar == '>') {
  int Next = peek();
  int Tok = tok_greater;
  if (Next == '=')
    Tok = (advance(), tok_geq);
  else if (Next == '>')
    Tok = (advance(), tok_shift_right);
  LexerLastChar = advance();
  return Tok;
}
```

## Parsing the New Tiers

`ParseShift`, `ParseRelational`, `ParseEquality`, `ParseBitwiseAnd`, `ParseBitwiseXor`, and `ParseBitwiseOr` all follow the same shape every tier has used since [Chapter 18](chapter-18.md): a base case that descends one level, and a `*Right` helper consuming a run of same-tier operators through `MergeBinaryExpression`. `ParseShift`, the innermost new tier, is representative of all six:

```cpp
static unique_ptr<ExpressionNode>
ParseShiftRight(unique_ptr<ExpressionNode> Left) {
  while (CurrentToken == tok_shift_left || CurrentToken == tok_shift_right) {
    int Operator = CurrentToken;
    getNextToken();
    auto Right = ParseSum();
    if (!Right)
      return nullptr;
    Left = MergeBinaryExpression(Operator, std::move(Left), std::move(Right));
    if (!Left)
      return nullptr;
  }
  return Left;
}

static unique_ptr<ExpressionNode> ParseShift() {
  auto Left = ParseSum();
  if (!Left)
    return nullptr;
  return ParseShiftRight(std::move(Left));
}
```

`ParseRelational` calls `ParseShift` for its base case and its operands; `ParseEquality` calls `ParseRelational`; `ParseBitwiseAnd` calls `ParseEquality`; and so on up through `ParseBitwiseOr`, which `ParseLogicalAnd` now calls instead of the old `ParseComparison`. Six new tiers, same recursive-descent pattern throughout — nothing here needed a general precedence-climbing mechanism, since pyxc doesn't have one.

## Type-Checking Predicates for Bitwise and Shift Operators

Two predicates identify the new operator families, built on the real token names the lexer produces:

```cpp
static bool IsBitwiseOp(int Operator) {
  return Operator == tok_ampersand || Operator == tok_pipe ||
         Operator == tok_caret;
}

static bool IsShiftOp(int Operator) {
  return Operator == tok_shift_left || Operator == tok_shift_right;
}
```

`GetBinaryResultType` gains two new branches. For bitwise ops, both operands must be integers; `IsAssignable` picks the wider of the two as the result type, same widening rule every other integer binary op already uses:

```cppdiff
*static ValueType GetBinaryResultType(int Operator, ValueType L, ValueType R) {
*  ...
*  if (IsLogicalOp(Operator)) {
*    if (L == ValueType::Bool && R == ValueType::Bool)
*      return ValueType::Bool;
*    return ValueType::Error;
*  }
+  if (IsBitwiseOp(Operator)) {
+    if (!IsIntType(L) || !IsIntType(R))
+      return ValueType::Error;
+    if (IsAssignable(L, R))
+      return L;
+    if (IsAssignable(R, L))
+      return R;
+    return ValueType::Error;
+  }
*  return ValueType::Error;
*}
```

For shifts, the result type is always the left operand's own type, regardless of the shift count's type:

```cppdiff
*static ValueType GetBinaryResultType(int Operator, ValueType L, ValueType R) {
*  ...
*  if (IsBitwiseOp(Operator)) {
*    ...
*  }
+  if (IsShiftOp(Operator)) {
+    if (!IsIntType(L) || !IsIntType(R))
+      return ValueType::Error;
+    return L;
+  }
*  return ValueType::Error;
*}
```

Both checks run inside `GetBinaryResultType`, the same function every binary operator's type checking has gone through since [Chapter 18](chapter-18.md), so type errors are caught before `MergeBinaryExpression` ever builds a node — codegen never sees a bad operand pair.

## Parsing Unary `~`

`~` is parsed in `ParseFactor` alongside `-` and `!`, the same tier [Chapter 21](chapter-21.md) added `!` to. The operand must already be an integer type; the result type is the same as the operand's:

```cppdiff
*static unique_ptr<ExpressionNode> ParseFactor() {
*  if (CurrentToken == tok_minus)
*    return ParseUnaryMinus();
*  if (CurrentToken == tok_exclamation) {
*    ...
*  }
+  if (CurrentToken == tok_tilde) {
+    getNextToken(); // eat '~'
+    auto Operand = ParseFactor();
+    if (!Operand)
+      return nullptr;
+    if (!IsIntType(Operand->getType()))
+      return LogErrorExpression("Unary '~' requires an integer operand");
+    ValueType OperandType = Operand->getType();
+    return make_unique<UnaryExpressionNode>(tok_tilde, std::move(Operand),
+                                             OperandType);
+  }
*  return ParsePrimary();
*}
```

`~~x` (double complement) and `~(x + 1)` both parse naturally, since the operand is a full `ParseFactor()` call, letting the recursion handle any nesting.

## Codegen: Binary Bitwise and Shift Operators

`BinaryExpressionNode::codegen`'s existing `switch (Operator)` gains cases for `tok_ampersand`, `tok_pipe`, `tok_caret`, and the two shift tokens. Both operands are coerced to the result type via `EmitImplicitCast` first — this is what handles the widening `GetBinaryResultType` already decided on (e.g. `int32 & int64` widens the `int32` side before the instruction):

```cpp
case tok_ampersand:
case tok_pipe:
case tok_caret: {
  ValueType ResultType = getType();
  L = EmitImplicitCast(L, LType, ResultType);
  R = EmitImplicitCast(R, RType, ResultType);
  if (!L || !R)
    return LogErrorValue("Type mismatch in binary operator");
  if (Operator == tok_ampersand)
    return TheBuilder->CreateAnd(L, R, "bwand");
  if (Operator == tok_pipe)
    return TheBuilder->CreateOr(L, R, "bwor");
  return TheBuilder->CreateXor(L, R, "bwxor");
}
case tok_shift_left:
case tok_shift_right: {
  R = EmitCast(R, RType, LType);
  if (!R)
    return LogErrorValue("Type mismatch in shift operator");
  if (Operator == tok_shift_left)
    return TheBuilder->CreateShl(L, R, "shltmp");
  return IsUnsignedIntType(LType)
             ? TheBuilder->CreateLShr(L, R, "shrtmp")
             : TheBuilder->CreateAShr(L, R, "shrtmp");
}
```

Each bitwise operator maps to a single LLVM instruction: `and`, `or`, or `xor`. These are integer-only instructions; LLVM has no floating-point equivalent, which is consistent with `GetBinaryResultType` already rejecting non-integer operands.

`CreateShl` emits `shl`, shifting left and filling low bits with zero. Right shift dispatches on the left operand's signedness, the same `IsUnsignedIntType` check every arithmetic operator has used since [Chapter 19](chapter-19.md) added unsigned types: `CreateAShr` (`ashr`, arithmetic, sign-extending) for signed integers, `CreateLShr` (`lshr`, logical, zero-filling) for unsigned ones. For a negative signed value, `x >> 1` stays negative because the vacated high bits fill with the sign bit; for a `uint32`, the same shift fills with zero, since there's no sign to preserve.

## Codegen: Unary `~`

`UnaryExpressionNode::codegen` gains a case for `tok_tilde` alongside the existing `tok_minus` case:

```cppdiff
*Value *UnaryExpressionNode::codegen() {
*  ...
*  if (Opcode == tok_exclamation)
*    return TheBuilder->CreateNot(Operator, "nottmp");
*
+  if (Opcode == tok_tilde)
+    return TheBuilder->CreateNot(Operator, "bnottmp");
*
*  return LogErrorValue("Invalid unary operator: " + FormatTokenForMessage(Opcode));
*}
```

`CreateNot` lowers to `xor %val, -1`: XOR-ing every bit against a mask of all ones flips each one. The instruction name `bnottmp` (bitwise not) distinguishes it in the IR from `nottmp`, the name [Chapter 21](chapter-21.md)'s logical `!` uses for its `i1` negation.

For a concrete example:

```pyxc
var x: int = 9     # binary: ...0001001
var y: int = ~x    # binary: ...1110110 → -10 in two's complement
var z: int = y & 7 # mask the low 3 bits → 6
```

`~9` is `-10` because in two's complement, flipping every bit and adding one negates a value: `~x` is always `-(x + 1)`.

## Known Limitations

**No hexadecimal, octal, or binary integer literals.** `0xFF`, `0o17`, and `0b101` all fail to parse; only decimal digits are recognized. I ran into this directly while writing the intro example — I'd originally written `0xFF` and had to switch to `255`.

**No compound assignment for bitwise or shift operators.** `x &= mask`, `flags |= bit`, `x ^= pattern`, `x <<= 2`, and `x >>= 1` all fail to parse. [Chapter 35](chapter-35.md)'s compound-assignment mechanism is general — `IsAssignmentOperator` and `AssignmentBinaryOperator` could, in principle, be extended to cover `&=`, `|=`, `^=`, `<<=`, and `>>=` the same way they cover `+=` through `%=` — but this chapter doesn't add the tokens or the table entries to do it. I confirmed this by trying `x &= mask` directly and getting a parse error, not a working compound assignment.

## Try It

**Bitwise operator on a float is a type error**

```pyxc
def main() -> int:
  var x: float64 = 1.0
  var y: float64 = 2.0
  var z: float64 = x & y
  return 0
```

```text
Error (Line 4, Column 25): Type mismatch in binary operator
  var z: float64 = x & y
                        ^~~~
Error (Line 5, Column 11): cannot return a value from a None function
  return 0
          ^~~~
```

**`~` on a non-integer is a type error**

```pyxc
def main() -> int:
  var x: float64 = 1.0
  var y: float64 = ~x
  return 0
```

```text
Error (Line 3, Column 22): Unary '~' requires an integer operand
  var y: float64 = ~x
                     ^~~~
Error (Line 4, Column 11): cannot return a value from a None function
  return 0
          ^~~~
```

Both are caught while parsing and never reach codegen.

## Build and Run

```bash
cd code/chapter-22
cmake -S . -B build && cmake --build build
```

```bash
llvm-lit -v test/
```

## What's Next

[Chapter 23](chapter-23.md) adds `switch`.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.
