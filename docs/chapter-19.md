---
description: "Add unsigned integer types uint8, uint16, uint32, and uint64 with correct unsigned arithmetic, comparisons, and casts throughout."
---
# 19. pyxc: Unsigned Integer Types

## What I Am Building

I've had signed integers since [Chapter 18](chapter-18.md), but all of them interpret their top bit as a sign. Sizes, counts, and raw memory offsets are commonly stored as unsigned values in systems code, and without unsigned types I have no way to generate the right instructions for them — division is the sharpest example, since signed and unsigned division of the same bit pattern can give wildly different answers. After this chapter, `uint8`, `uint16`, `uint32`, and `uint64` are available:

<!-- code-merge:start -->
```pyxc
extern def printd(x: float64)

def main() -> int:
  var x: uint32 = uint32(-1)   # reinterpreted as 4294967295
  printd(float64(x / uint32(2)))

  var a: uint32 = 7
  var b: uint32 = 3
  printd(float64(a % b))

  return 0
```
```text
2147483647.000000
1.000000
```
<!-- code-merge:end -->

`uint32(-1)` doesn't produce a negative number — the bit pattern for `-1` reinterpreted as unsigned is `4294967295`, and dividing that unsigned value by `2` gives `2147483647` (`udiv`, truncating). Read as signed, that same bit pattern divided by `2` would give `-1` back (`sdiv`, rounds toward zero) — a completely different answer from the identical bits.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-19
```

## Grammar

I add `uint8`, `uint16`, `uint32`, and `uint64` to `type` and `cast-type`, the only two productions that name concrete integer types. Nothing else in the grammar changes:

`code/chapter-19/pyxc.ebnf`

```grammardiff
*...
*name                              = (letter | "_")
*                                    { letter | digit | "_" } ;
-type                              = "int" | "int8" | "int16" | "int32"
-                                    | "int64"
+type                              = "int" | "int8" | "int16" | "int32"
+                                    | "int64" | "uint8" | "uint16"
+                                    | "uint32" | "uint64"
*                                    | "float" | "float32"
*                                    | "float64" | "bool" | "None" ;
-cast-type                         = "int" | "int8" | "int16" | "int32"
-                                    | "int64"
+cast-type                         = "int" | "int8" | "int16" | "int32"
+                                    | "int64" | "uint8" | "uint16"
+                                    | "uint32" | "uint64"
*                                    | "float" | "float32"
*                                    | "float64" | "bool" ;
*...
```

## New Tokens, Keywords, and `ValueType` Enum Values

Four new tokens and keywords:

```cppdiff
*enum Token {
*  ...
*  tok_break = -37,
*  tok_continue = -38,
+  tok_uint8 = -39,
+  tok_uint16 = -40,
+  tok_uint32 = -41,
+  tok_uint64 = -42,
*
*  // punctuation and operators
*  tok_lparen = '(',
*  ...
*};
```

```cppdiff
*static map<string, Token> Keywords = {
*    ...
*    {"int", tok_int},         {"int8", tok_int8},       {"int16", tok_int16},
-    {"int32", tok_int32},     {"int64", tok_int64},     {"float", tok_float},
+    {"int32", tok_int32},     {"int64", tok_int64},
+    {"uint8", tok_uint8},     {"uint16", tok_uint16},
+    {"uint32", tok_uint32},   {"uint64", tok_uint64},
+    {"float", tok_float},
*    {"float32", tok_float32}, {"float64", tok_float64}, {"bool", tok_bool},
*    {"None", tok_none},       {"True", tok_true},       {"False", tok_false}};
```

Four new values in the `ValueType` enum:

```cppdiff
*enum class ValueType {
*  None,
*  Int, /* depends on system default for int */
*  Int8,
*  Int16,
*  Int32,
*  Int64,
+  UInt8,
+  UInt16,
+  UInt32,
+  UInt64,
*  Float,
*  Float32,
*  Float64,
*  Bool,
*  Error
*};
```

I give `ParseTypeToken` cases for all four so they work in type annotations and the `cast-type` production:

```cppdiff
*static ValueType ParseTypeToken() {
*  switch (CurrentToken) {
*  case tok_int:
*    getNextToken();
*    return ValueType::Int;
*  ...
*  case tok_int64:
*    getNextToken();
*    return ValueType::Int64;
+  case tok_uint8:
+    getNextToken();
+    return ValueType::UInt8;
+  case tok_uint16:
+    getNextToken();
+    return ValueType::UInt16;
+  case tok_uint32:
+    getNextToken();
+    return ValueType::UInt32;
+  case tok_uint64:
+    getNextToken();
+    return ValueType::UInt64;
*  case tok_float:
*    ...
*  }
*}
```

## No New LLVM IR Types

LLVM has no separate "unsigned integer" types. `uint32` and `int32` are both `i32` in the IR. I map the four new `ValueType` values to the same LLVM types as their signed counterparts, in `LLVMTypeFor`:

```cppdiff
*static Type *LLVMTypeFor(ValueType Type) {
*  switch (Type) {
*  case ValueType::Int: {
*    ...
*  }
*  case ValueType::Int8:
*    return Type::getInt8Ty(*TheContext);
*  case ValueType::Int16:
*    return Type::getInt16Ty(*TheContext);
*  case ValueType::Int32:
*    return Type::getInt32Ty(*TheContext);
*  case ValueType::Int64:
*    return Type::getInt64Ty(*TheContext);
+  case ValueType::UInt8:
+    return Type::getInt8Ty(*TheContext);
+  case ValueType::UInt16:
+    return Type::getInt16Ty(*TheContext);
+  case ValueType::UInt32:
+    return Type::getInt32Ty(*TheContext);
+  case ValueType::UInt64:
+    return Type::getInt64Ty(*TheContext);
*  case ValueType::Float:
*    ...
*  }
*}
```

The signedness lives entirely in which instruction I emit. This also matches C's representation: `size_t` maps to `uint64` on a 64-bit target, so that's what I declare when a parameter or return value on an `extern def` is a C `size_t`.

## Signed and Unsigned Predicates

I add a new predicate function that drives all instruction selection:

```cpp
static bool IsUnsignedIntType(ValueType Type) {
  return Type == ValueType::UInt8 || Type == ValueType::UInt16 ||
         Type == ValueType::UInt32 || Type == ValueType::UInt64;
}
```

Every signed/unsigned branch in codegen is a call to `IsUnsignedIntType`; there is no separate `IsSignedIntType` helper, since everywhere that needs "signed" just means "not unsigned" in context.

I expand `IsIntType` to include all four unsigned types:

```cpp
static bool IsIntType(ValueType Type) {
  return Type == ValueType::Int8 || Type == ValueType::Int16 ||
         Type == ValueType::Int32 || Type == ValueType::Int ||
         Type == ValueType::Int64 || Type == ValueType::UInt8 ||
         Type == ValueType::UInt16 || Type == ValueType::UInt32 ||
         Type == ValueType::UInt64;
}
```

## Implicit Widening Rule — Same Signedness Only

I give `CanWidenInt` a signedness gate. `IsAssignable` itself is unchanged: it still just calls `CanWidenInt` for the integer-to-integer case, but that helper now rejects mixed signedness before comparing the bit widths it's been comparing since Chapter 18:

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

## Instruction Selection — Six Changed Sites

### Integer Widening

```cpp
// Before: always sext
return TheBuilder->CreateSExt(V, LLVMTypeFor(To), "sext");

// After:
return IsUnsignedIntType(From)
           ? TheBuilder->CreateZExt(V, LLVMTypeFor(To), "zext")
           : TheBuilder->CreateSExt(V, LLVMTypeFor(To), "sext");
```

Unsigned types use `zext` (zero-extend) rather than `sext` (sign-extend).

### Integer → Float

```cppdiff
*static Value *EmitCast(Value *V, ValueType From, ValueType To) {
*  ...
*  // Integer ↔ float conversions.
-  if (IsIntType(From) && IsFloatType(To))
-    return TheBuilder->CreateSIToFP(V, LLVMTypeFor(To), "sitofp");
+  if (IsIntType(From) && IsFloatType(To))
+    return IsUnsignedIntType(From)
+               ? TheBuilder->CreateUIToFP(V, LLVMTypeFor(To), "uitofp")
+               : TheBuilder->CreateSIToFP(V, LLVMTypeFor(To), "sitofp");
*  ...
*}
```

`uitofp` treats the bit pattern as an unsigned integer, producing the correct positive float for `uint32(-1)` = 4294967295.0. `uint64(-1)` is `18446744073709551615`; converting that to `float64` rounds, since `float64` only represents integers exactly up to `2^53`.

### Float → Integer

```cppdiff
*static Value *EmitCast(Value *V, ValueType From, ValueType To) {
*  ...
-  if (IsFloatType(From) && IsIntType(To))
-    return TheBuilder->CreateFPToSI(V, LLVMTypeFor(To), "fptosi");
+  if (IsFloatType(From) && IsIntType(To))
+    return IsUnsignedIntType(To)
+               ? TheBuilder->CreateFPToUI(V, LLVMTypeFor(To), "fptoui")
+               : TheBuilder->CreateFPToSI(V, LLVMTypeFor(To), "fptosi");
*  ...
*}
```

### Division and Remainder

```cppdiff
*    if (Operator == tok_plus)
*      return TheBuilder->CreateAdd(L, R, "addtmp");
*    if (Operator == tok_minus)
*      return TheBuilder->CreateSub(L, R, "subtmp");
-    if (Operator == tok_slash)
-      return TheBuilder->CreateSDiv(L, R, "divtmp");
-    if (Operator == tok_percent)
-      return TheBuilder->CreateSRem(L, R, "remtmp");
+    if (Operator == tok_slash)
+      return IsUnsignedIntType(getType())
+                 ? TheBuilder->CreateUDiv(L, R, "divtmp")
+                 : TheBuilder->CreateSDiv(L, R, "divtmp");
+    if (Operator == tok_percent)
+      return IsUnsignedIntType(getType())
+                 ? TheBuilder->CreateURem(L, R, "remtmp")
+                 : TheBuilder->CreateSRem(L, R, "remtmp");
*    return TheBuilder->CreateMul(L, R, "multmp");
```

### Comparisons (`<`, `<=`, `>`, `>=`)

```cppdiff
*    } else {
*      switch (Operator) {
*      case tok_less:
-        return TheBuilder->CreateICmpSLT(L, R, "cmptmp");
+        return IsUnsignedIntType(CompareType)
+                   ? TheBuilder->CreateICmpULT(L, R, "cmptmp")
+                   : TheBuilder->CreateICmpSLT(L, R, "cmptmp");
*      case tok_greater:
-        return TheBuilder->CreateICmpSGT(L, R, "cmptmp");
+        return IsUnsignedIntType(CompareType)
+                   ? TheBuilder->CreateICmpUGT(L, R, "cmptmp")
+                   : TheBuilder->CreateICmpSGT(L, R, "cmptmp");
*      case tok_eq:
*        return TheBuilder->CreateICmpEQ(L, R, "cmptmp");
*      case tok_neq:
*        return TheBuilder->CreateICmpNE(L, R, "cmptmp");
*      case tok_leq:
-        return TheBuilder->CreateICmpSLE(L, R, "cmptmp");
+        return IsUnsignedIntType(CompareType)
+                   ? TheBuilder->CreateICmpULE(L, R, "cmptmp")
+                   : TheBuilder->CreateICmpSLE(L, R, "cmptmp");
*      case tok_geq:
-        return TheBuilder->CreateICmpSGE(L, R, "cmptmp");
+        return IsUnsignedIntType(CompareType)
+                   ? TheBuilder->CreateICmpUGE(L, R, "cmptmp")
+                   : TheBuilder->CreateICmpSGE(L, R, "cmptmp");
*      default:
*        break;
*      }
*    }
```

`==` and `!=` are signedness-agnostic (`icmp eq` / `icmp ne`); they are unchanged.

### Literal Range Check

`ParseNumberExpression` already checks that a literal fits in the target type. I update the max-value calculation to use `APInt::getAllOnes(Bits)` for unsigned types:

```cppdiff
*    APInt Val(ParseBits, NumberLiteral, 10);
*
-    APInt Max = APInt::getSignedMaxValue(Bits);
+    APInt Max = IsUnsignedIntType(Type) ? APInt::getAllOnes(Bits)
+                                        : APInt::getSignedMaxValue(Bits);
*    if (Val.ugt(Max))
*      return LogErrorExpression("Integer literal out of range for type");
```

`getAllOnes` is the all-bits-set value (`0xFF`, `0xFFFF`, etc.), which is the maximum for an unsigned type. `getSignedMaxValue` is `0x7F`, `0x7FFF`, etc.

## REPL Printing: Unsigned Results Print as `%llu`

Chapter 18's JIT dispatch switch picks a function pointer type and a `printf` format per `ValueType`. I add a case for each of the four unsigned types, alongside the existing signed ones:

```cpp
case ValueType::UInt32: {
  uint32_t (*FP)() = ExprSymbol.toPtr<uint32_t (*)()>();
  unsigned long long result = static_cast<unsigned long long>(FP());
  if (IsRepl && LastTopLevelShouldPrint)
    fprintf(stdout, "%llu\n", result);
  break;
}
```

`UInt8`, `UInt16`, and `UInt64` follow the identical pattern with their own `toPtr` type. The cast to `unsigned long long` and the `%llu` format specifier mean a top-level `uint32(-1)` in the REPL prints `4294967295`, not a negative number: the same distinction that drives every other signed/unsigned instruction choice in this chapter applies here too, just at the print boundary instead of in generated IR.

## Explicit Casts

I always allow explicit casts between signed and unsigned types. They reinterpret the bit pattern:

```pyxc
var x: int32  = -1
var y: uint32 = uint32(x)   # 4294967295
var z: int32  = int32(y)    # -1
```

Same bit width: bits are unchanged. Narrowing truncates to the low bits.

## Build and Run

```bash
cd code/chapter-19
cmake -S . -B build && cmake --build build
./build/pyxc
```

```bash
llvm-lit -v test/
```

## Try It

<!-- code-merge:start -->
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
  printd(float64(ui))
  return 0
```
```text
1.000000
0.000000
4294967295.000000
```
<!-- code-merge:end -->

Same 32 bits, `si` and `ui`. As `int32`, that bit pattern is negative. As `uint32`, it's not — `ui < uint32(0)` can never be true, since there's no such thing as a negative `uint32`. Read back as unsigned, the same bits print as `4294967295`, not `-1`.

## What's Next

[Chapter 20](chapter-20.md) adds `-g` debug info, now with real types to describe.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.
