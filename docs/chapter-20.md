---
description: "Add pointer arithmetic: ptr + int, ptr - int, ptr - ptr, and pointer comparisons: so you can walk memory with a pointer."
---
# 20. pyxc: Pointer Arithmetic

## What I Am Building

[Chapter 19](chapter-19.md) gave me pointers: `addr` to take an address, `p[i]` to index, and pointer parameters so functions can modify the caller's data. What I couldn't do yet was walk a pointer forward or backward, or compare two pointers. The classic K&R summing-loop pattern, compute an end pointer, then advance `p` until it equals `end`, was out of reach.

After this chapter, that pattern works:

```pyxc
extern def printd(x: float64)

struct Triple:
  a: int
  b: int
  c: int

def main() -> int:
  var t: Triple
  t.a = 10
  t.b = 20
  t.c = 30
  var p: ptr[int] = addr(t.a)
  var end: ptr[int] = p + 3
  var total: int = 0
  for var i: int = 0, p + i != end, 1:
    total = total + p[i]
  printd(float64(total))  # 60.000000
  return 0
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-20
```

## Grammar

No grammar change this chapter — `code/chapter-20/pyxc.ebnf` is byte-identical to chapter 19's. `+`, `-`, and the comparison operators already existed as `sum`/`comparison` productions; nothing new needs to be parsed. What changes is purely semantic: `GetBinaryResultType` now accepts pointer operands and returns pointer or integer types accordingly, so the same syntax that already parsed `a + b` for two integers now also accepts one pointer and one integer operand.

## Semantic Rules

| Expression | Operand types | Result type |
|---|---|---|
| `p + n` | `ptr[T]`, int | `ptr[T]` |
| `n + p` | int, `ptr[T]` | `ptr[T]` |
| `p - n` | `ptr[T]`, int | `ptr[T]` |
| `p - q` | `ptr[T]`, `ptr[T]` (same T) | `int64` |
| `p < q`, `p > q`, etc. | `ptr[T]`, `ptr[T]` (same T) | `bool` |
| `p * n` | `ptr[T]`, int | **type error** |
| `p + q` | `ptr[T]`, `ptr[U]` | **type error** |
| `p - q` | `ptr[T]`, `ptr[U]` (T ≠ U) | **type error** |

Pointer difference (`p - q`) yields an element count, not bytes. If `p` and `q` are both `ptr[int]` and they are 24 bytes apart, `p - q` is 3: the number of `int`-sized steps between them.

Multiplication by a pointer is blocked. There is no sensible meaning for `ptr[T] * int` in terms of memory addresses.

## Extending Binary Result Type Resolution

`GetBinaryResultType` is the central function that decides what type a binary expression produces. Its signature is extended to carry struct-name information for both operands and to write the result struct name back:

```cpp
if (IsArithmeticOp(Operator)) {
  if (Operator != tok_star &&
      ((L == ValueType::Pointer && IsIntType(R)) ||
       (R == ValueType::Pointer && IsIntType(L)))) {
    if (ResultStructName)
      *ResultStructName = (L == ValueType::Pointer) ? LStruct : RStruct;
    return ValueType::Pointer;
  }
  if (Operator == tok_minus && L == ValueType::Pointer && R == ValueType::Pointer &&
      LStruct == RStruct)
    return ValueType::Int64;
  ...
}
if (IsComparisonOp(Operator)) {
  if (L == ValueType::Pointer && R == ValueType::Pointer &&
      LStruct == RStruct)
    return ValueType::Bool;
  ...
}
```

For `ptr + int` and `int + ptr`, the result inherits the struct name (which encodes the pointee type) from whichever operand is the pointer. For `ptr - ptr`, no struct name is needed because the result is a plain `int64`. For pointer comparisons, the result is `bool`.

Mismatched pointer types (`ptr[int] - ptr[float64]`) fall through to the default error path because `LStruct != RStruct`.

## Extending the Binary Expression Constructor

`BinaryExpressionNode` gains an optional `StructName` parameter so the pointer type of an arithmetic result can be carried through the AST. `Type` itself stays required, only the new `StructName` gets a default:

```cpp
BinaryExpressionNode(int Operator, unique_ptr<ExpressionNode> Left, unique_ptr<ExpressionNode> Right,
              ValueType Type, const string &StructName = "")
```

The constructor calls `setType(Type, StructName)`. Before this chapter, `MergeBinaryExpression` had no struct name to pass in, because no binary result could be a pointer. Now it does, whether or not the particular result happens to be one.

## One Merge Path for Every Operator

I don't need a separate branch for pointer results. `MergeBinaryExpression` already existed from [Chapter 19](chapter-19.md) as the one place every binary-operator parser (`ParseTermRight`, and its counterparts for `sum` and `comparison`) funnels through. I just extend the two calls it makes, `GetBinaryResultType` and the `BinaryExpressionNode` constructor, to carry struct names in both directions:

```cpp
static unique_ptr<ExpressionNode>
MergeBinaryExpression(int Operator, unique_ptr<ExpressionNode> Left,
                      unique_ptr<ExpressionNode> Right) {
  string ResultStructName;
  ValueType ResultType =
      GetBinaryResultType(Operator, Left->getType(), Left->getStructName(),
                          Right->getType(), Right->getStructName(),
                          &ResultStructName);
  if (ResultType == ValueType::Error)
    return LogErrorExpression("Type mismatch in binary operator");
  return make_unique<BinaryExpressionNode>(
      Operator, std::move(Left), std::move(Right), ResultType,
      ResultStructName);
}
```

Every binary expression, pointer arithmetic or not, goes through this exact same function unchanged. `ResultStructName` is empty for anything that isn't a pointer result, so the extension is free for the common case: it costs one more argument to thread through, not a new code path.

## Codegen: `ptr + int` and `int + ptr`

Advancing a pointer by an integer uses `CreateInBoundsGEP`. The integer index is widened to `i64` first:

```cpp
if (getType() == ValueType::Pointer) {
  Value *Ptr = nullptr;
  Value *Idx = nullptr;
  if (LType == ValueType::Pointer && IsIntType(RType) && Operator == tok_plus) {
    Ptr = L; Idx = EmitImplicitCast(R, RType, ValueType::Int64);
  } else if (RType == ValueType::Pointer && IsIntType(LType) && Operator == tok_plus) {
    Ptr = R; Idx = EmitImplicitCast(L, LType, ValueType::Int64);
  } else if (LType == ValueType::Pointer && IsIntType(RType) && Operator == tok_minus) {
    Ptr = L; Idx = EmitImplicitCast(R, RType, ValueType::Int64);
    if (Idx) Idx = Builder->CreateNeg(Idx, "negidx");
  }
  if (!Ptr || !Idx)
    return LogErrorV("Type mismatch in arithmetic");
  // ...
  return Builder->CreateInBoundsGEP(ElemLLVM, Ptr, Idx, "ptrarith");
}
```

`DecodePointerType` extracts the element LLVM type from the result's encoded struct name. This is the type that GEP uses to compute its stride. I use the `inbounds` variant, `CreateInBoundsGEP`, which tells LLVM's optimizer the pointer arithmetic never leaves the bounds of the allocation it started in: that assumption enables optimizations a plain GEP can't get, at the cost of undefined behavior if the assumption is ever wrong. This chapter doesn't check that assumption at runtime (see Known Limitations).

For `p + 1` where `p: ptr[int]`, compiled and read directly:

```llvm
%p2 = load ptr, ptr %p1
%ptrarith = getelementptr inbounds i64, ptr %p2, i64 1
```

## Codegen: `ptr - int`

Subtracting an integer is the same as adding its negation. The index is widened to `i64` and then negated with `CreateNeg` before being passed to GEP:

```llvm
%p2 = load ptr, ptr %p1
%negidx = sub i64 0, 2
%ptrarith = getelementptr inbounds i64, ptr %p2, i64 %negidx
```

For `p - 2`, GEP steps backward by two elements.

## Codegen: `ptr - ptr`

Pointer subtraction uses LLVM's `CreatePtrDiff`, which handles the ptrtoint / subtract / divide sequence internally:

```cpp
if (Op == '-' && getType() == ValueType::Int64 &&
    LType == ValueType::Pointer && RType == ValueType::Pointer) {
  // ...
  return Builder->CreatePtrDiff(ElemLLVM, L, R, "ptrdiff");
}
```

`CreatePtrDiff` takes the element type so it can divide the byte difference by `sizeof(T)` to produce an element count. For two `ptr[int]` values that are 24 bytes apart, the result is 3.

```llvm
; q - p where p and q are both ptr[int]
%ptrdiff = ...  ; ptrtoint both, subtract, sdiv by sizeof(i64) = 8
```

The element type is decoded from the left operand's struct name encoding using `DecodePointerType`.

## Codegen: Pointer Comparisons

Pointer comparisons use unsigned integer comparison instructions. Addresses are non-negative, so unsigned comparison gives the correct ordering:

```cpp
if (LType == ValueType::Pointer && RType == ValueType::Pointer &&
    Left->getStructName() == Right->getStructName()) {
  switch (Operator) {
  case tok_eq:      return Builder->CreateICmpEQ(L, R, "cmptmp");
  case tok_neq:     return Builder->CreateICmpNE(L, R, "cmptmp");
  case tok_less:    return Builder->CreateICmpULT(L, R, "cmptmp");
  case tok_greater: return Builder->CreateICmpUGT(L, R, "cmptmp");
  case tok_leq:     return Builder->CreateICmpULE(L, R, "cmptmp");
  case tok_geq:     return Builder->CreateICmpUGE(L, R, "cmptmp");
  }
}
```

Using `ICmpULT` (unsigned less-than) rather than `ICmpSLT` (signed) is correct here. On 64-bit platforms, pointer values are 64-bit integers and addresses in the upper half of the address space would appear negative under signed comparison. Unsigned comparison treats all addresses as non-negative and orders them correctly.

## Build and Run

```bash
cd code/chapter-20
cmake -S . -B build && cmake --build build
```

## Try It

### Advance a pointer and read the next element

```pyxc
extern def printd(x: float64)

def main() -> int:
  var a: int = 10
  var b: int = 20
  var p: ptr[int] = addr(a)
  printd(float64(p[0]))    # 10.000000
  p = p + 1
  printd(float64(p[0]))    # 20.000000 (b is next on the stack: layout-dependent)
  return 0
```

```bash
10.000000
20.000000
```

### Walk backward with `p - 1`

```pyxc
extern def printd(x: float64)

struct Pair:
  first: int
  second: int

def main() -> int:
  var pair: Pair
  pair.first = 100
  pair.second = 200
  var p: ptr[int] = addr(pair.second)
  printd(float64(p[0]))    # 200.000000
  p = p - 1
  printd(float64(p[0]))    # 100.000000
  return 0
```

```bash
200.000000
100.000000
```

### Compute pointer difference between two fields

```pyxc
extern def printd(x: float64)

struct Triple:
  a: int
  b: int
  c: int

def main() -> int:
  var t: Triple
  var pa: ptr[int] = addr(t.a)
  var pc: ptr[int] = addr(t.c)
  printd(float64(pc - pa))  # 2.000000 (two int-sized steps from a to c)
  return 0
```

```bash
2.000000
```

### An end-pointer loop with `!= end`

```pyxc
extern def printd(x: float64)

struct Triple:
  a: int
  b: int
  c: int

def main() -> int:
  var t: Triple
  t.a = 10
  t.b = 20
  t.c = 30
  var p: ptr[int] = addr(t.a)
  var end: ptr[int] = p + 3
  var total: int = 0
  for var i: int = 0, p + i != end, 1:
    total = total + p[i]
  printd(float64(total))  # 60.000000
  return 0
```

```bash
60.000000
```

### Inspect the IR

```bash
pyxc --emit llvm-ir -o out.ll program.pyxc
grep 'getelementptr\|ptrdiff\|icmp' out.ll
```

## Known Limitations

**No bounds checking.** Out-of-bounds pointer arithmetic is silent undefined behavior. There is no runtime check and no compile-time warning.

**`ptr * int` is intentionally rejected.** Multiplying a pointer by an integer has no defined memory meaning. The type checker blocks it.

**Mismatched pointer types are a type error.** `ptr[int] + ptr[float64]` and `ptr[int] - ptr[float64]` are both rejected. Only pointers with identical encoded struct names can interact.

**`p - q` yields element count, not bytes.** If you need the byte distance, multiply by the element size manually. There is no `sizeof` operator yet: that comes in [Chapter 21](chapter-21.md).

**No pointer-to-integer casts.** You cannot convert a pointer to an integer to inspect its numeric address.

## What's Next

[Chapter 21](chapter-21.md) adds `malloc`, `free`, and `sizeof`: heap allocation built directly on the pointer arithmetic from this chapter. With `p + n`, `p - q`, and a way to allocate arbitrary memory, dynamic data structures become possible.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.
