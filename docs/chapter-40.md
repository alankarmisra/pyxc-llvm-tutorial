---
description: "Add unsigned integer types uint8, uint16, uint32, and uint64 with correct unsigned arithmetic, comparisons, and casts throughout."
---
# 40. pyxc: Unsigned Integer Types

## What I Am Building

[Chapter 39](chapter-39.md) added Unicode support to character and string literals. I've had signed integers since [Chapter 17](chapter-17.md), but all of them interpret their top bit as a sign. Sizes, counts, and bit masks are commonly stored as unsigned values in systems code, and without unsigned types I have no way to generate the right instructions for them. After this chapter, `uint8`, `uint16`, `uint32`, and `uint64` are available:

```pyxc
extern def printd(x: float64)

def main() -> int:
  var flags: uint32 = 0
  flags = flags | uint32(1) << uint32(3)   # set bit 3
  flags = flags | uint32(1) << uint32(7)   # set bit 7

  var mask: uint32 = uint32(255)
  printd(float64(flags & mask))            # 136.000000
  return 0
```

```
136.000000
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-40
```

## Grammar

I add `uint8`, `uint16`, `uint32`, and `uint64` to `builtin-type` and `cast-type`, the only two productions that name concrete integer types:

`code/chapter-40/pyxc.ebnf`

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
                 { end-of-lines "elif" expression ":" suite }
                 [ end-of-lines "else" ":" suite ] ;
 while-statement       = "while" expression ":" suite ;
 do-while-statement     = "do" ":" suite end-of-lines "while" expression ;
 switch-statement      = "switch" expression ":" end-of-lines indent switch-body dedent ;
 switch-body      = switch-case { end-of-lines switch-case } [ end-of-lines default-case ] ;
 switch-case      = "case" switch-integer { "," switch-integer } ":" suite ;
 default-case     = "default" ":" suite ;
 for-statement         = "for"
                   ( "var" name ":" type | name )
                   "=" expression "," expression "," expression ":" suite ;
 variable-statement         = "var" variable-binding { "," variable-binding } ;
 assignment-statement      = lvalue assignment-operator expression ; (* assignment is a statement here *)
 simple-statement      = return-statement | break-statement | continue-statement | variable-statement | assignment-statement | expression ;
 compound-statement    = if-statement | for-statement | while-statement | do-while-statement | switch-statement ;
 statement       = simple-statement | compound-statement ;
 suite           = simple-statement | compound-statement | end-of-lines block ;
 return-statement      = "return" [ expression ] ;
 break-statement       = "break" ;
 continue-statement    = "continue" ;
 statement-separator = end-of-lines | BLOCK_END ;
 block = indent statement { statement-separator statement } dedent ;
 expression      = logical-or ;
 logical-or      = logical-and { "||" logical-and } ;
 logical-and     = bitwise-or { "&&" bitwise-or } ;
 bitwise-or      = bitwise-xor { "|" bitwise-xor } ;
 bitwise-xor     = bitwise-and { "^" bitwise-and } ;
 bitwise-and     = equality { "&" equality } ;
 equality        = relational { ("==" | "!=") relational } ;
 relational      = shift { ("<" | "<=" | ">" | ">=") shift } ;
 shift           = sum { ("<<" | ">>") sum } ;
 sum             = term { ("+" | "-") term } ;
 term            = unary-expression { ("*" | "/" | "%") unary-expression } ;
 lvalue          = name | field-access | index-expression ;
 variable-binding      = name ":" type [ "=" expression ] ;
 unary-expression       = ("-" | "!" | "~" | "++" | "--") unary-expression | postfix-expression ;
 postfix-expression     = primary [ postfix-operator ] ;
 postfix-operator       = "++" | "--" ;
 primary         = cast-expression | sizeof-expression | address-expression | array-literal | string-literal | character-literal | name-expression | field-access | index-expression | number-expression | boolean-literal | parenthesized-expression ;
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
 string-literal   = "\"" { ? valid Unicode scalar value except " and newline, encoded as UTF-8 ? | literal-escape } "\"" ;
 character-literal     = "'" ( ? valid Unicode scalar value except ' and newline, encoded as UTF-8 ? | literal-escape ) "'" ;
 literal-escape   = "\\" ( simple-escape | octal-escape | "x" hex-digit hex-digit
                    | "u" hex-digit hex-digit hex-digit hex-digit
                    | "U" hex-digit hex-digit hex-digit hex-digit
                          hex-digit hex-digit hex-digit hex-digit ) ;
 simple-escape    = "a" | "b" | "f" | "n" | "r" | "t" | "v"
                  | "\\" | "'" | "\"" | "?" ;
 octal-escape     = octal-digit [ octal-digit [ octal-digit ] ] ;
 parenthesized-expression       = "(" expression ")" ;
 indent          = INDENT ;
 dedent          = DEDENT ;

 assignment-operator        = "=" | "+=" | "-=" | "*=" | "/=" | "%=" ;
 name      = (letter | "_") { letter | digit | "_" } ;
 builtin-type     = "int" | "int8" | "int16" | "int32" | "int64"
+                | "uint8" | "uint16" | "uint32" | "uint64"
                 | "float" | "float32" | "float64"
                 | "bool" | "None" ;
 alias-type       = name ;
 struct-type      = name ;
 pointer-type     = "ptr" "[" type "]" ;
 type            = base-type [ array-suffix ] ;
 base-type        = builtin-type | alias-type | struct-type | pointer-type ;
 array-suffix     = "[" integer "]" ;
 cast-type        = "int" | "int8" | "int16" | "int32" | "int64"
+                | "uint8" | "uint16" | "uint32" | "uint64"
                 | "float" | "float32" | "float64"
                 | "bool" | pointer-type ;
 integer         = digit { digit } ;
 switch-integer       = [ "-" ] integer ;
 number          = ( digit { digit } [ "." { digit } ]
                   | "." digit { digit } ) [ exponent ] ;
 exponent        = ( "e" | "E" ) [ "+" | "-" ] digit { digit } ;
 boolean-literal    = "True" | "False" ;
 letter          = "A".."Z" | "a".."z" ;
 digit           = "0".."9" ;
 hex-digit       = digit | "A".."F" | "a".."f" ;
 octal-digit     = "0".."7" ;
 end-of-line             = "\r\n" | "\r" | "\n" ;
 comment = "#" { comment-character } ;
 comment-character = ? any character except "\r" and "\n" ? ;
 whitespace = " " | "\t" | "\v" | "\f" ;
 INDENT          = ? synthetic token emitted by lexer ? ;
 DEDENT          = ? synthetic token emitted by lexer ? ;

 BLOCK_END = ? synthetic token injected into the stream by ParseBlock immediately after it consumes DEDENT ? ;
```

## New Tokens, Keywords, and `ValueType` Enum Values

Four new tokens and keywords:

```cpp
tok_uint8  = -65,
tok_uint16 = -66,
tok_uint32 = -67,
tok_uint64 = -68,
```

```cpp
{"uint8", tok_uint8}, {"uint16", tok_uint16},
{"uint32", tok_uint32}, {"uint64", tok_uint64},
```

Four new values in the `ValueType` enum:

```cpp
UInt8,
UInt16,
UInt32,
UInt64,
```

I give `ParseTypeToken` cases for all four so they work in type annotations and the `cast-type` production:

```cpp
case tok_uint8:  getNextToken(); BaseType = ValueType::UInt8;  break;
case tok_uint16: getNextToken(); BaseType = ValueType::UInt16; break;
case tok_uint32: getNextToken(); BaseType = ValueType::UInt32; break;
case tok_uint64: getNextToken(); BaseType = ValueType::UInt64; break;
```

## No New LLVM IR Types

LLVM has no separate "unsigned integer" types. `uint32` and `int32` are both `i32` in the IR. I map the four new `ValueType` values to the same LLVM types as their signed counterparts, in `LLVMTypeFor`:

```cpp
case ValueType::UInt8:  return Type::getInt8Ty(*TheContext);
case ValueType::UInt16: return Type::getInt16Ty(*TheContext);
case ValueType::UInt32: return Type::getInt32Ty(*TheContext);
case ValueType::UInt64: return Type::getInt64Ty(*TheContext);
```

The signedness lives entirely in which instruction I emit. This also matches C's representation: `size_t` maps to `uint64` on a 64-bit target, so that's what I declare when a parameter or return value on an `extern def` is a C `size_t`.

## Signed and Unsigned Predicates

I add two new predicate functions that drive all instruction selection:

```cpp
static bool IsUnsignedIntType(ValueType Type) {
  return Type == ValueType::UInt8 || Type == ValueType::UInt16 ||
         Type == ValueType::UInt32 || Type == ValueType::UInt64;
}

static bool IsSignedIntType(ValueType Type) {
  return IsIntType(Type) && !IsUnsignedIntType(Type);
}
```

I expand `IsIntType` to include all four unsigned types:

```cpp
return Type == ValueType::Int || Type == ValueType::Int8 || ... ||
       Type == ValueType::UInt8 || Type == ValueType::UInt16 ||
       Type == ValueType::UInt32 || Type == ValueType::UInt64;
```

## Implicit Widening Rule — Same Signedness Only

I give `CanWidenInt` a signedness gate. `IsAssignable` itself is unchanged: it still just calls `CanWidenInt` for the integer-to-integer case, but that helper now rejects mixed signedness before comparing the bit widths it's been comparing since Chapter 17:

```cpp
static bool CanWidenInt(ValueType From, ValueType To) {
  if (From == To)
    return true;
  if (IsIntType(From) && IsIntType(To)) {
    if (IsUnsignedIntType(From) != IsUnsignedIntType(To))
      return false;
    unsigned FromBits = LLVMTypeFor(From)->getIntegerBitWidth();
    unsigned ToBits = LLVMTypeFor(To)->getIntegerBitWidth();
    return FromBits <= ToBits;
  }
  return false;
}
```

`uint8 → uint64` widens without a cast. `int32 → uint32` or `uint32 → int64` requires an explicit cast. This matches my design intent: implicit signed/unsigned conversion is a common bug source in C, and I don't want pyxc doing it silently.

```pyxc
var a: uint32 = 1
var b: int32  = 2
a = a + b
```
```
Error (Line 3, Column 10): Type mismatch in binary operator
a = a + b
         ^~~~
```

Cast explicitly to fix it: `a = a + uint32(b)`.

## Instruction Selection — Seven Changed Sites

### Integer Widening

```cpp
// Before: always sext
return Builder->CreateSExt(V, LLVMTypeFor(To), "sext");

// After:
return IsUnsignedIntType(From)
           ? Builder->CreateZExt(V, LLVMTypeFor(To), "zext")
           : Builder->CreateSExt(V, LLVMTypeFor(To), "sext");
```

Unsigned types use `zext` (zero-extend) rather than `sext` (sign-extend).

### Integer → float

```cpp
return IsUnsignedIntType(From)
           ? Builder->CreateUIToFP(V, LLVMTypeFor(To), "uitofp")
           : Builder->CreateSIToFP(V, LLVMTypeFor(To), "sitofp");
```

`uitofp` treats the bit pattern as an unsigned integer, producing the correct positive float for `uint32(-1)` = 4294967295.0. `uint64(-1)` is `18446744073709551615`; converting that to `float64` rounds, since `float64` only represents integers exactly up to `2^53`.

### Float → integer

```cpp
return IsUnsignedIntType(To)
           ? Builder->CreateFPToUI(V, LLVMTypeFor(To), "fptoui")
           : Builder->CreateFPToSI(V, LLVMTypeFor(To), "fptosi");
```

### Division and remainder

```cpp
// / operator:
return IsUnsignedIntType(ResultType) ? Builder->CreateUDiv(L, R, "divtmp")
                                     : Builder->CreateSDiv(L, R, "divtmp");
// % operator:
return IsUnsignedIntType(ResultType) ? Builder->CreateURem(L, R, "modtmp")
                                     : Builder->CreateSRem(L, R, "modtmp");
```

### Right shift

```cpp
return IsUnsignedIntType(Ty) ? Builder->CreateLShr(L, R, "shrtmp")
                              : Builder->CreateAShr(L, R, "shrtmp");
```

`lshr` fills vacated high bits with zero, `ashr` fills with the sign bit, so right shift is always logical for unsigned types: `uint32(-1) >> 1` gives `2147483647`, not a sign-extended `4294967295`.

### Comparisons (`<`, `<=`, `>`, `>=`)

```cpp
// '<':
return IsUnsignedIntType(CompareType)
           ? Builder->CreateICmpULT(L, R, "cmptmp")
           : Builder->CreateICmpSLT(L, R, "cmptmp");
// '>':
return IsUnsignedIntType(CompareType)
           ? Builder->CreateICmpUGT(L, R, "cmptmp")
           : Builder->CreateICmpSGT(L, R, "cmptmp");
// '<=':
return IsUnsignedIntType(CompareType)
           ? Builder->CreateICmpULE(L, R, "cmptmp")
           : Builder->CreateICmpSLE(L, R, "cmptmp");
// '>=':
return IsUnsignedIntType(CompareType)
           ? Builder->CreateICmpUGE(L, R, "cmptmp")
           : Builder->CreateICmpSGE(L, R, "cmptmp");
```

`==` and `!=` are signedness-agnostic (`icmp eq` / `icmp ne`); they are unchanged.

### Literal range check

`ParseNumberExpression` already checks that a literal fits in the target type. I update the max-value calculation to use `APInt::getAllOnes(Bits)` for unsigned types:

```cpp
APInt Max = IsUnsignedIntType(Type) ? APInt::getAllOnes(Bits)
                                    : APInt::getSignedMaxValue(Bits);
```

`getAllOnes` is the all-bits-set value (`0xFF`, `0xFFFF`, etc.), which is the maximum for an unsigned type. `getSignedMaxValue` is `0x7F`, `0x7FFF`, etc.

## Explicit Casts

I always allow explicit casts between signed and unsigned types. They reinterpret the bit pattern:

```pyxc
var x: int32  = -1
var y: uint32 = uint32(x)   # 4294967295
var z: int32  = int32(y)    # -1
```

Same bit width: bits are unchanged. Narrowing truncates to the low bits.

## Try It

```pyxc
extern def printd(x: float64)

def main() -> int:
  var si: int32 = -1
  var ui: uint32 = uint32(si)
  if si < 0:
    printd(1.0)
  else:
    printd(0.0)
  if ui < uint32(0):
    printd(1.0)
  else:
    printd(0.0)
  printd(float64(ui >> uint32(1)))
  return 0
```

```
1.000000
0.000000
2147483647.000000
```

Same 32 bits, `si` and `ui`. As `int32`, that bit pattern is negative. As `uint32`, it's not — `ui < uint32(0)` can never be true, since there's no such thing as a negative `uint32`. And shifting it right doesn't sign-extend: I get `2147483647`, not a value with the top bit still set.

## What's Next

[Chapter 41](chapter-41.md) allows assignment to appear inside an expression — enabling the `while (c = getchar()) != EOF` pattern from K&R.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.
