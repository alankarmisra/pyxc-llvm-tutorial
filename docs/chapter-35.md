---
description: "Add bitwise operators &, |, ^, <<, >> and unary ~ with C-standard precedence and integer-only type checking."
---
# 35. pyxc: Bitwise Operators

## What I Am Building

I'm pretty much done with the loop story after [Chapter 34](chapter-34.md). The last major gap before K&R-style systems programming is bitwise manipulation. If I add that, I can use flags, masks, and bit-shifting in my code and crack more of the K&R-style problems. Here's what I'm aiming to get working:

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
cd pyxc-llvm-tutorial/code/chapter-35
```

## Grammar

`&`, `|`, `^`, `<<`, and `>>` each get their own grammar tier, following C's precedence ordering. The old flat `comparison` production splits into `equality` and `relational`, and three new bitwise tiers slot in around them; `unary-expression` gains `~`:

```grammardiff
 program         = [ end-of-lines ] [ top-level-item { end-of-lines top-level-item } ] [ end-of-lines ] ;
 end-of-lines            = end-of-line { end-of-line } ;
 top-level-item             = type-alias | trait-definition | struct-definition | class-definition | implementation-definition | function-definition | external | top-level-expression ;
 type-alias       = "type" name "=" type ;
 trait-definition        = "trait" name [ "[" name "]" ] ":" end-of-lines trait-block ;
 trait-block      = indent trait-method-signature { end-of-lines trait-method-signature } dedent ;
 trait-method-signature  = "def" name "(" [ typed-parameter { "," typed-parameter } ] ")" [ "->" type ] ;
 struct-definition       = "struct" name ":" end-of-lines struct-block ;
 class-definition        = "class" name [ "(" trait-reference { "," trait-reference } ")" ] ":" end-of-lines struct-block ;
 trait-reference        = name [ "[" type "]" ] ;
 implementation-definition         = "impl" trait-reference "for" name ":" end-of-lines implementation-block ;
 implementation-block       = indent implementation-method { end-of-lines implementation-method } dedent ;
 implementation-method      = "def" name "(" [ typed-parameter { "," typed-parameter } ] ")" [ "->" type ] ":" ( simple-statement | end-of-lines block ) ;
 struct-block     = indent class-member { end-of-lines class-member } dedent ;
 class-member     = [ visibility ] ( field-declaration | method-definition ) ;
 visibility      = "public" | "private" ;
 method-definition       = "def" name "(" [ typed-parameter { "," typed-parameter } ] ")"
                   [ "->" type ] ":" ( simple-statement | end-of-lines block ) ;
 field-declaration       = name ":" type ;
 function-definition      = "def" function-signature [ "->" type ] ":" ( simple-statement | end-of-lines block ) ;
 (* If the return type is omitted, it defaults to None. *)
 external        = "extern" "def" function-signature [ "->" type ] ;
 top-level-expression    = expression ;
 function-signature       = name "(" [ typed-parameter { "," typed-parameter } ] ")" ;
 typed-parameter      = name ":" type ;
 if-statement          = "if" expression ":" suite
                 [ end-of-lines "else" ":" suite ] ;
 for-statement         = "for"
                   ( "var" name ":" type | name )
                   "=" expression "," expression "," expression ":" suite ;
 while-statement       = "while" expression ":" suite ;
 do-while-statement     = "do" ":" suite end-of-lines "while" expression ;
 variable-statement         = "var" variable-binding { "," variable-binding } ;
 assignment-statement      = lvalue assignment-operator expression ; (* assignment is a statement here *)
 simple-statement      = return-statement | break-statement | continue-statement | variable-statement | assignment-statement | expression ;
 compound-statement    = if-statement | for-statement | while-statement | do-while-statement ;
 statement       = simple-statement | compound-statement ;
 suite           = simple-statement | compound-statement | end-of-lines block ;
 return-statement      = "return" [ expression ] ;
 break-statement       = "break" ;
 continue-statement    = "continue" ;
 statement-separator = end-of-lines | BLOCK_END ;
 block = indent statement { statement-separator statement } dedent ;
 expression      = logical-or ;
 logical-or      = logical-and { "||" logical-and } ;
-logical-and     = comparison { "&&" comparison } ;
-comparison      = sum { comparison-operator sum } ;
-comparison-operator = "==" | "!=" | "<=" | ">=" | "<" | ">" ;
+logical-and     = bitwise-or { "&&" bitwise-or } ;
+bitwise-or      = bitwise-xor { "|" bitwise-xor } ;
+bitwise-xor     = bitwise-and { "^" bitwise-and } ;
+bitwise-and     = equality { "&" equality } ;
+equality        = relational { ("==" | "!=") relational } ;
+relational      = shift { ("<" | "<=" | ">" | ">=") shift } ;
+shift           = sum { ("<<" | ">>") sum } ;
 sum             = term { ("+" | "-") term } ;
 term            = unary-expression { ("*" | "/" | "%") unary-expression } ;
 lvalue          = name | field-access | index-expression ;
 variable-binding      = name ":" type [ "=" expression ] ;
-unary-expression       = ("-" | "!" | "++" | "--") unary-expression | postfix-expression ;
+unary-expression       = ("-" | "!" | "~" | "++" | "--") unary-expression | postfix-expression ;
 postfix-expression     = primary [ postfix-operator ] ;
 postfix-operator       = "++" | "--" ;
 primary         = cast-expression | sizeof-expression | address-expression | array-literal | string-literal | name-expression | field-access | index-expression | number-expression | boolean-literal | parenthesized-expression ;
 cast-expression        = cast-type "(" expression ")" ;
 sizeof-expression      = "sizeof" "(" type ")" ;
 address-expression        = "addr" "(" lvalue ")" ;
 name-expression  = name | call-expression | method-call-expression | constructor-call-expression ;
 call-expression        = name "(" [ expression { "," expression } ] ")" ;
 method-call-expression  = name "." name "(" [ expression { "," expression } ] ")" ;
 constructor-call-expression    = name "(" [ expression { "," expression } ] ")" ;
 field-access     = name "." name { "." name } ;
 index-expression       = name "[" expression "]" ;
 number-expression      = number ;
 array-literal    = "[" [ expression { "," expression } ] "]" ;
 string-literal   = "\"" { ? any char except " and newline ? | escape } "\"" ;
 escape          = "\\" ( "\\" | "\"" | "n" | "t" | "0" ) ;
 parenthesized-expression       = "(" expression ")" ;
 indent          = INDENT ;
 dedent          = DEDENT ;
 
 assignment-operator        = "=" | "+=" | "-=" | "*=" | "/=" | "%=" ;
 name      = (letter | "_") { letter | digit | "_" } ;
 builtin-type     = "int" | "int8" | "int16" | "int32" | "int64"
                 | "float" | "float32" | "float64"
                 | "bool" | "None" ;
 alias-type       = name ;
 struct-type      = name ;
 pointer-type     = "ptr" "[" type "]" ;
 type            = base-type [ array-suffix ] ;
 base-type        = builtin-type | alias-type | struct-type | pointer-type ;
 array-suffix     = "[" integer "]" ;
 cast-type        = "int" | "int8" | "int16" | "int32" | "int64"
                 | "float" | "float32" | "float64"
                 | "bool" | pointer-type ;
 integer         = digit { digit } ;
 number          = ( digit { digit } [ "." { digit } ]
                   | "." digit { digit } ) [ exponent ] ;
 exponent        = ( "e" | "E" ) [ "+" | "-" ] digit { digit } ;
 boolean-literal    = "True" | "False" ;
 letter          = "A".."Z" | "a".."z" ;
 digit           = "0".."9" ;
 end-of-line             = "\r\n" | "\r" | "\n" ;
 comment = "#" { comment-character } ;
 comment-character = ? any character except "\r" and "\n" ? ;
 whitespace = " " | "\t" | "\v" | "\f" ;
 INDENT          = ? synthetic token emitted by lexer ? ;
 DEDENT          = ? synthetic token emitted by lexer ? ;
 
 BLOCK_END = ? synthetic token injected into the stream by ParseBlock immediately after it consumes DEDENT ? ;
```

`bitwise-and` sits directly above `equality`, which is exactly what produces C's famous precedence gotcha: since each side of `&` is a full `equality` (which can itself contain `==`), `a & b == 0` parses as `a & (b == 0)`, not `(a & b) == 0`. I hit this myself while testing rather than just asserting it: `a & b == 0` for integer `a`, `b` is a real type error, "Type mismatch in binary operator", precisely because it parses as `a & (b == 0)` and `&` refuses a `bool` operand on the right. Getting `(a & b) == 0` requires the parentheses.

## New Tokens for `<<` and `>>`

Single-character operators like `&`, `|`, `^`, and `~` already fall through the lexer's catch-all ASCII path, returning their own character values as tokens. `<<` and `>>` are two-character, so they need real token values:

```cpp
tok_shl = -58, // <<
tok_shr = -59, // >>
```

## Lexer Peek-Ahead for Shifts

The existing `<` and `>` paths already peeked one character ahead for `=` (to produce `<=`/`>=`). I extend them to also check for a second `<` or `>`:

```cpp
if (LexerLastChar == '<') {
  int Next = peek();
  int Tok = tok_less;
  if (Next == '=')
    Tok = (advance(), tok_leq);   // '<=' — comparison
  else if (Next == '<')
    Tok = (advance(), tok_shl);   // '<<' — left shift
  LexerLastChar = advance();
  return Tok;
}

if (LexerLastChar == '>') {
  int Next = peek();
  int Tok = tok_greater;
  if (Next == '=')
    Tok = (advance(), tok_geq);   // '>=' — comparison
  else if (Next == '>')
    Tok = (advance(), tok_shr);   // '>>' — right shift
  LexerLastChar = advance();
  return Tok;
}
```

## Parsing the New Tiers

`ParseShift`, `ParseRelational`, `ParseEquality`, `ParseBitwiseAnd`, `ParseBitwiseXor`, and `ParseBitwiseOr` all follow the same shape every tier has used since [Chapter 20](chapter-20.md): a base case that descends one level, and a `*Right` helper consuming a run of same-tier operators through `MergeBinaryExpression`. `ParseShift`, the innermost new tier, is representative of all six:

```cpp
static unique_ptr<ExpressionNode>
ParseShiftRight(unique_ptr<ExpressionNode> Left) {
  while (CurrentToken == tok_shl || CurrentToken == tok_shr) {
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
static bool IsBitwiseOp(int Operator) { return Operator == tok_ampersand || Operator == tok_pipe || Operator == tok_caret; }
static bool IsShiftOp(int Operator) { return Operator == tok_shl || Operator == tok_shr; }
```

`GetBinaryResultType` gains two new branches. For bitwise ops, both operands must be integers; `IsAssignable` picks the wider of the two as the result type, same widening rule every other integer binary op already uses:

```cpp
if (IsBitwiseOp(Operator)) {
  if (!IsIntType(L) || !IsIntType(R))
    return ValueType::Error;
  if (IsAssignable(L, R))
    return L;
  if (IsAssignable(R, L))
    return R;
  return ValueType::Error;
}
```

For shifts, the result type is always the left operand's own type, regardless of the shift count's type:

```cpp
if (IsShiftOp(Operator)) {
  if (!IsIntType(L) || !IsIntType(R))
    return ValueType::Error;
  return L;
}
```

Both checks run inside `GetBinaryResultType`, the same function every binary operator's type checking has gone through since [Chapter 20](chapter-20.md), so type errors are caught before `MergeBinaryExpression` ever builds a node — codegen never sees a bad operand pair.

## Parsing Unary `~`

`~` is parsed in `ParseUnary` alongside `-`, `!`, and prefix `++`/`--`. The operand must already be an integer type; the result type is the same as the operand's:

```cpp
if (CurrentToken == tok_tilde) {
  getNextToken(); // eat '~'
  auto Operand = ParseUnary();
  if (!Operand)
    return nullptr;
  if (!IsIntType(Operand->getType()))
    return LogErrorExpression("Unary '~' requires an integer operand");
  ValueType OperandType = Operand->getType();
  return make_unique<UnaryExpressionNode>(tok_tilde, std::move(Operand),
                                          OperandType);
}
```

`~~x` (double complement) and `~(x + 1)` both parse naturally, since the operand is a full `ParseUnary()` call, letting the recursion handle any nesting.

## Codegen: Binary Bitwise and Shift Operators

`BinaryExpressionNode::codegen`'s existing `switch (Operator)` gains cases for `tok_ampersand`, `tok_pipe`, `tok_caret`, and the two shift tokens. Both operands are coerced to the result type via `EmitImplicitCast` first — this is what handles the widening `GetBinaryResultType` already decided on (e.g. `int32 & int64` widens the `int32` side before the instruction):

```cpp
case tok_ampersand:
case tok_pipe:
case tok_caret: {
  ValueType Ty = getType();
  L = EmitImplicitCast(L, LType, Ty);
  R = EmitImplicitCast(R, RType, Ty);
  if (!L || !R)
    return LogErrorV("Type mismatch in binary operator");
  if (Operator == tok_ampersand)
    return Builder->CreateAnd(L, R, "bwand");
  if (Operator == tok_pipe)
    return Builder->CreateOr(L, R, "bwor");
  return Builder->CreateXor(L, R, "bwxor");
}
case tok_shl:
case tok_shr: {
  ValueType Ty = getType();
  L = EmitImplicitCast(L, LType, Ty);
  R = EmitImplicitCast(R, RType, Ty);
  if (!L || !R)
    return LogErrorV("Type mismatch in binary operator");
  if (Operator == tok_shl)
    return Builder->CreateShl(L, R, "shltmp");
  return Builder->CreateAShr(L, R, "shrtmp");
}
```

Each bitwise operator maps to a single LLVM instruction: `and`, `or`, or `xor`. These are integer-only instructions; LLVM has no floating-point equivalent, which is consistent with `GetBinaryResultType` already rejecting non-integer operands.

`CreateShl` emits `shl`, shifting left and filling low bits with zero. `CreateAShr` emits `ashr`, an arithmetic (sign-extending) right shift: for a negative value, `x >> 1` stays negative because the vacated high bits fill with the sign bit rather than zero. LLVM also has `CreateLShr` for a logical (zero-filling) right shift, but pyxc doesn't expose it here — `ashr` is the correct choice as long as every integer type is signed, which is still true at this point in the tutorial.

## Codegen: Unary `~`

`UnaryExpressionNode::codegen` gains a case for `tok_tilde` alongside the existing `tok_minus` case:

```cpp
if (Opcode == tok_tilde) {
  if (!IsIntType(getType()))
    return LogErrorV("Unary '~' not supported for this type");
  return Builder->CreateNot(Operator, "bnottmp");
}
```

`CreateNot` lowers to `xor %val, -1`: XOR-ing every bit against a mask of all ones flips each one. The instruction name `bnottmp` (bitwise not) distinguishes it in the IR from `nottmp`, the name [Chapter 33](chapter-33.md)'s logical `!` uses for its `i1` negation.

For a concrete example:

```pyxc
var x: int = 9     # binary: ...0001001
var y: int = ~x    # binary: ...1110110 → -10 in two's complement
var z: int = y & 7 # mask the low 3 bits → 6
```

`~9` is `-10` because in two's complement, flipping every bit and adding one negates a value: `~x` is always `-(x + 1)`.

## Known Limitations

**No hexadecimal, octal, or binary integer literals.** `0xFF`, `0o17`, and `0b101` all fail to parse; only decimal digits are recognized. I ran into this directly while writing the intro example — I'd originally written `0xFF` and had to switch to `255`.

**No compound assignment for bitwise or shift operators.** `x &= mask`, `flags |= bit`, `x ^= pattern`, `x <<= 2`, and `x >>= 1` all fail to parse. [Chapter 32](chapter-32.md)'s compound-assignment mechanism is general — `IsCompoundAssignTok` and `CompoundAssignToBinaryOp` could, in principle, be extended to cover `&=`, `|=`, `^=`, `<<=`, and `>>=` the same way they cover `+=` through `%=` — but this chapter doesn't add the tokens or the table entries to do it. I confirmed this by trying `x &= mask` directly and getting a parse error, not a working compound assignment.

**Right shift is always arithmetic (sign-extending).** There's no unsigned integer type yet for a logical right shift to make sense on; every integer type is signed through this chapter.

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
```

Both are caught while parsing and never reach codegen.

## Build and Run

```bash
cd code/chapter-35
cmake -S . -B build && cmake --build build
```

## What's Next

[Chapter 36](chapter-36.md) adds `switch` statements.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.
