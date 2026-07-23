---
description: "Add unsigned integer types uint8, uint16, uint32, and uint64 with correct unsigned arithmetic, comparisons, and casts throughout."
---
# 39. pyxc: Unsigned Integer Types

## Where We Are

[Chapter 38](chapter-38.md) added character literals. pyxc has had signed integers since [Chapter 17](chapter-17.md), but all of them interpret their top bit as a sign. Sizes, counts, and bit masks are commonly stored as unsigned values in systems code, and without unsigned types the compiler has no way to generate the right instructions for them. After this chapter, `uint8`, `uint16`, `uint32`, and `uint64` are available:

```pyxc
extern def printd(x: float64)

def main() -> int:
  var flags: uint32 = 0
  flags |= uint32(1) << uint32(3)   # set bit 3
  flags |= uint32(1) << uint32(7)   # set bit 7

  var mask: uint32 = uint32(0xFF)
  printd(float64(flags & mask))     # 136.000000
  return 0
```

```
136.000000
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-39
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

`ParseTypeToken` gets cases for all four so they work in type annotations and the `casttype` production:

```cpp
case tok_uint8:  getNextToken(); BaseType = ValueType::UInt8;  break;
case tok_uint16: getNextToken(); BaseType = ValueType::UInt16; break;
case tok_uint32: getNextToken(); BaseType = ValueType::UInt32; break;
case tok_uint64: getNextToken(); BaseType = ValueType::UInt64; break;
```

## No New LLVM IR Types

LLVM has no separate "unsigned integer" types. `uint32` and `int32` are both `i32` in the IR. `LLVMTypeFor` maps the four new `ValueType` values to the same LLVM types as their signed counterparts:

```cpp
case ValueType::UInt8:  return Type::getInt8Ty(*TheContext);
case ValueType::UInt16: return Type::getInt16Ty(*TheContext);
case ValueType::UInt32: return Type::getInt32Ty(*TheContext);
case ValueType::UInt64: return Type::getInt64Ty(*TheContext);
```

The signedness lives entirely in which instruction the compiler emits.

## `IsUnsignedIntType` and `IsSignedIntType`

Two new predicate functions drive all instruction selection:

```cpp
static bool IsUnsignedIntType(ValueType Type) {
  return Type == ValueType::UInt8 || Type == ValueType::UInt16 ||
         Type == ValueType::UInt32 || Type == ValueType::UInt64;
}

static bool IsSignedIntType(ValueType Type) {
  return IsIntType(Type) && !IsUnsignedIntType(Type);
}
```

`IsIntType` is expanded to include all four unsigned types:

```cpp
return Type == ValueType::Int || Type == ValueType::Int8 || ... ||
       Type == ValueType::UInt8 || Type == ValueType::UInt16 ||
       Type == ValueType::UInt32 || Type == ValueType::UInt64;
```

## Implicit Widening Rule — Same Signedness Only

`IsAssignable` gains a signedness gate. The bit-width comparison added in the previous chapter is now also gated on signedness:

```cpp
if (IsIntType(From) && IsIntType(To)) {
  unsigned FromBits = LLVMTypeFor(From)->getIntegerBitWidth();
  unsigned ToBits   = LLVMTypeFor(To)->getIntegerBitWidth();
  if (IsUnsignedIntType(From) != IsUnsignedIntType(To))
    return false;          // signed/unsigned mixing forbidden implicitly
  return FromBits <= ToBits;
}
```

`uint8 → uint64` widens without a cast. `int32 → uint32` or `uint32 → int64` requires an explicit cast. This matches the design intent: implicit signed/unsigned conversion is a common bug source in C; pyxc won't do it silently.

## Instruction Selection — Seven Changed Sites

### Integer widening (`EmitImplicitCast`)

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

`uitofp` treats the bit pattern as an unsigned integer, producing the correct positive float for `uint32(-1)` = 4294967295.0.

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

`lshr` fills vacated high bits with zero. `ashr` fills with the sign bit.

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

`ParseNumberExpr` already checks that a literal fits in the target type. The max value calculation is updated to use `APInt::getAllOnes(Bits)` for unsigned types:

```cpp
APInt Max = IsUnsignedIntType(Type) ? APInt::getAllOnes(Bits)
                                    : APInt::getSignedMaxValue(Bits);
```

`getAllOnes` is the all-bits-set value (`0xFF`, `0xFFFF`, etc.), which is the maximum for an unsigned type. `getSignedMaxValue` is `0x7F`, `0x7FFF`, etc.

## Explicit Casts

Explicit casts between signed and unsigned types are always allowed. They reinterpret the bit pattern:

```pyxc
var x: int32  = -1
var y: uint32 = uint32(x)   # 4294967295
var z: int32  = int32(y)    # -1
```

Same bit width: bits are unchanged. Narrowing truncates to the low bits.

## Grammar

```ebnf
builtintype = "int" | "int8" | "int16" | "int32" | "int64"
            | "uint8" | "uint16" | "uint32" | "uint64"   -- changed
            | "float" | "float32" | "float64"
            | "bool" | "None" ;
casttype    = "int" | "int8" | "int16" | "int32" | "int64"
            | "uint8" | "uint16" | "uint32" | "uint64"   -- changed
            | "float" | "float32" | "float64"
            | "bool" | pointertype ;
```

## Error Cases

**Implicit signed/unsigned mix:**
```pyxc
var a: uint32 = 1
var b: int32  = 2
a = a + b   # Error: Type mismatch
```

Cast explicitly: `a = a + uint32(b)`.

## Things Worth Knowing

**`uint64(-1)` is `18446744073709551615`.** Converting it to `float64` rounds up because `float64` can only represent integers exactly up to `2^53`.

**Right shift is always logical for unsigned types.** `uint32(-1) >> 1` fills the vacated high bit with zero, giving `2147483647`.

**`size_t` maps to `uint64` on 64-bit targets.** When calling C functions that take or return `size_t`, declare the parameter as `uint64`.

## What's Next

[Chapter 40](chapter-40.md) allows assignment to appear inside an expression — enabling the `while (c = getchar()) != EOF` pattern from K&R.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.
