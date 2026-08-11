---
description: "Add pointer arithmetic: ptr + int, ptr - int, ptr - ptr, and pointer comparisons: so you can walk memory with a pointer."
---
# 26. pyxc: Pointer Arithmetic

## What I Am Building

[Chapter 25](chapter-25.md) gave me pointers: `addr` to take an address, `p[i]` to index, and pointer parameters so functions can modify the caller's data. What I couldn't do yet was walk a pointer forward or backward, or compare two pointers. The classic K&R summing-loop pattern, compute an end pointer, then advance `p` until it equals `end`, was out of reach.

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
cd pyxc-llvm-tutorial/code/chapter-26
```

## Grammar

No grammar change this chapter — `code/chapter-26/pyxc.ebnf` is byte-identical to chapter 25's, aside from the header comment. `+`, `-`, and the comparison operators already existed as `sum`/`comparison` productions; nothing new needs to be parsed. What changes is purely semantic: `GetBinaryResultType` now accepts pointer operands and returns pointer or integer types accordingly, so the same syntax that already parsed `a + b` for two integers now also accepts one pointer and one integer operand.

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

`GetBinaryResultType` is the central function that decides what type a binary expression produces. Before this chapter its signature was `(int Operator, ValueType L, ValueType R)`, with no way to know what a pointer operand points to. It now takes `LTypeInfo` and `RTypeInfo`, the encoded struct-name string for each operand, so pointer operands can be told apart by pointee type:

```cpp
static ValueType GetBinaryResultType(int Operator, ValueType L,
                                     const string &LTypeInfo, ValueType R,
                                     const string &RTypeInfo) {
  if (IsArithmeticOp(Operator)) {
    if (Operator == tok_plus &&
        ((L == ValueType::Pointer && IsIntType(R)) ||
         (R == ValueType::Pointer && IsIntType(L))))
      return ValueType::Pointer;
    if (Operator == tok_minus && L == ValueType::Pointer && IsIntType(R))
      return ValueType::Pointer;
    if (Operator == tok_minus && L == ValueType::Pointer &&
        R == ValueType::Pointer && LTypeInfo == RTypeInfo)
      return ValueType::Int64;
    ...
  }
  if (IsComparisonOp(Operator)) {
    if (L == ValueType::Pointer && R == ValueType::Pointer)
      return LTypeInfo == RTypeInfo ? ValueType::Bool : ValueType::Error;
    ...
  }
```

`GetBinaryResultType` itself only decides the *type* (`Pointer`, `Int64`, `Bool`, or `Error`); it doesn't hand back a struct name. Working out which struct name to attach to a pointer result happens one level up, in `MergeBinaryExpression` (below).

`p + n` is handled by its own `tok_plus` check with the pointer/int pair in either order. `p - n` gets a separate `tok_minus` check for `Pointer`/int, and `p - q` gets a third check for `Pointer`/`Pointer` with matching `LTypeInfo`/`RTypeInfo`. `p * n` matches none of these arithmetic branches, so it falls through to the general numeric-operand checks later in the function and is rejected because a pointer is not `IsNumericType`.

Mismatched pointer types (`ptr[int] - ptr[float64]`) fall through to the default error path because `LTypeInfo != RTypeInfo`. The same `LTypeInfo == RTypeInfo` check gates pointer comparisons.

## Extending the Binary Expression Constructor

`BinaryExpressionNode` gains an optional `StructName` parameter so the pointer type of an arithmetic result can be carried through the AST. `Type` itself stays required, only the new `StructName` gets a default:

```cpp
BinaryExpressionNode(int Operator, unique_ptr<ExpressionNode> Left, unique_ptr<ExpressionNode> Right,
              ValueType Type, const string &StructName = "")
```

The constructor calls `setType(Type, StructName)`. Before this chapter, `MergeBinaryExpression` had no struct name to pass in, because no binary result could be a pointer. Now it does, whether or not the particular result happens to be one.

## One Merge Path for Every Operator

I don't need a separate branch for pointer results. `MergeBinaryExpression` already existed from [Chapter 25](chapter-25.md) as the one place every binary-operator parser (`ParseTermRight`, and its counterparts for `sum` and `comparison`) funnels through. I extend the call it makes to `GetBinaryResultType` to pass struct names in, and I add a small step after that call to work out what struct name (if any) the result carries, before passing it to the `BinaryExpressionNode` constructor:

```cpp
static unique_ptr<ExpressionNode>
MergeBinaryExpression(int Operator, unique_ptr<ExpressionNode> Left,
                      unique_ptr<ExpressionNode> Right) {
  ValueType ResultType =
      GetBinaryResultType(Operator, Left->getType(), Left->getStructName(),
                          Right->getType(), Right->getStructName());
  if (ResultType == ValueType::Error)
    return LogErrorExpression("Type mismatch in binary operator");
  string ResultTypeInfo;
  if (ResultType == ValueType::Pointer)
    ResultTypeInfo = Left->getType() == ValueType::Pointer
                         ? Left->getStructName()
                         : Right->getStructName();
  return make_unique<BinaryExpressionNode>(
      Operator, std::move(Left), std::move(Right), ResultType,
      ResultTypeInfo);
}
```

Every binary expression, pointer arithmetic or not, goes through this exact same function unchanged. `ResultTypeInfo` stays empty for anything that isn't a pointer result (`ResultType == ValueType::Pointer` is false), so the extension is free for the common case: it costs one more branch and one more constructor argument, not a new code path. When the result is a pointer, `MergeBinaryExpression` picks it up from whichever operand is the pointer, since for `ptr + int` and `int + ptr` exactly one side is.

## Codegen: `ptr + int` and `int + ptr`

Advancing a pointer by an integer uses `CreateInBoundsGEP`. The integer index is cast to `i64` first with `CreateIntCast`:

```cpp
if ((Operator == tok_plus || Operator == tok_minus) &&
    getType() == ValueType::Pointer) {
  Value *Pointer = nullptr;
  Value *Index = nullptr;
  if (LType == ValueType::Pointer && IsIntType(RType)) {
    Pointer = L;
    Index = R;
  } else if (Operator == tok_plus && RType == ValueType::Pointer &&
             IsIntType(LType)) {
    Pointer = R;
    Index = L;
  }
  if (!Pointer || !Index)
    return LogErrorV("Type mismatch in pointer arithmetic");
  ValueType IndexType = LType == ValueType::Pointer ? RType : LType;
  Index = Builder->CreateIntCast(Index, Type::getInt64Ty(*TheContext),
                                 !IsUnsignedIntType(IndexType), "ptrindex");
  if (Operator == tok_minus)
    Index = Builder->CreateNeg(Index, "negindex");
  ValueType ElementType = ValueType::Error;
  string ElementStructName;
  if (!DecodePointerType(getStructName(), ElementType, ElementStructName))
    return LogErrorV("Invalid pointer type metadata");
  return Builder->CreateInBoundsGEP(
      LLVMTypeFor(ElementType, ElementStructName), Pointer, Index,
      "ptrarith");
}
```

`DecodePointerType` decodes the result's encoded struct name back into a `ValueType` and, for struct pointees, a struct name. `LLVMTypeFor` then turns that pair into the actual LLVM element type GEP needs to compute its stride. I use the `inbounds` variant, `CreateInBoundsGEP`, which tells LLVM's optimizer the pointer arithmetic never leaves the bounds of the allocation it started in: that assumption enables optimizations a plain GEP can't get, at the cost of undefined behavior if the assumption is ever wrong. This chapter doesn't check that assumption at runtime (see Known Limitations).

For `p + 1` where `p: ptr[int]`, compiled and read directly:

```llvm
%p1 = load ptr, ptr %p, align 8
%ptrarith = getelementptr inbounds i64, ptr %p1, i64 1
```

## Codegen: `ptr - int`

Subtracting an integer is the same as adding its negation. The index is cast to `i64` and then negated with `CreateNeg` before being passed to GEP, using the same branch shown above:

```llvm
%negindex = sub i64 0, 2
%ptrarith = getelementptr inbounds i64, ptr %p1, i64 %negindex
```

For `p - 2`, GEP steps backward by two elements.

## Codegen: `ptr - ptr`

Pointer subtraction uses LLVM's `CreatePtrDiff`, which handles the ptrtoint / subtract / divide sequence internally:

```cpp
if (Operator == tok_minus && getType() == ValueType::Int64 &&
    LType == ValueType::Pointer && RType == ValueType::Pointer) {
  ValueType ElementType = ValueType::Error;
  string ElementStructName;
  if (!DecodePointerType(Left->getStructName(), ElementType,
                         ElementStructName))
    return LogErrorV("Invalid pointer type metadata");
  return Builder->CreatePtrDiff(
      LLVMTypeFor(ElementType, ElementStructName), L, R, "ptrdiff");
}
```

`CreatePtrDiff` takes the element type so it can divide the byte difference by `sizeof(T)` to produce an element count. For two `ptr[int]` values that are 24 bytes apart, the result is 3.

```llvm
; pc - pa where pa and pc are both ptr[int]
%0 = ptrtoint ptr %pc2 to i64
%1 = ptrtoint ptr %pa3 to i64
%2 = sub i64 %0, %1
%ptrdiff = sdiv exact i64 %2, ptrtoint (ptr getelementptr (i64, ptr null, i32 1) to i64)
```

The element type is decoded from the left operand's struct name encoding using `DecodePointerType`.

## Codegen: Pointer Comparisons

Pointer comparisons use unsigned integer comparison instructions. Addresses are non-negative, so unsigned comparison gives the correct ordering. Codegen doesn't need to recheck that the two pointer types match: `GetBinaryResultType` already rejected mismatched pointer comparisons at parse time, so by the time this code runs, `LType == RType == Pointer` is enough:

```cpp
if (LType == ValueType::Pointer && RType == ValueType::Pointer) {
  switch (Operator) {
  case tok_less:
    return Builder->CreateICmpULT(L, R, "cmptmp");
  case tok_greater:
    return Builder->CreateICmpUGT(L, R, "cmptmp");
  case tok_eq:
    return Builder->CreateICmpEQ(L, R, "cmptmp");
  case tok_neq:
    return Builder->CreateICmpNE(L, R, "cmptmp");
  case tok_leq:
    return Builder->CreateICmpULE(L, R, "cmptmp");
  case tok_geq:
    return Builder->CreateICmpUGE(L, R, "cmptmp");
  default:
    break;
  }
}
```

Using `ICmpULT` (unsigned less-than) rather than `ICmpSLT` (signed) is correct here. On 64-bit platforms, pointer values are 64-bit integers and addresses in the upper half of the address space would appear negative under signed comparison. Unsigned comparison treats all addresses as non-negative and orders them correctly.

## Build and Run

```bash
cd code/chapter-26
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

**`p - q` yields element count, not bytes.** If you need the byte distance, multiply by the element size manually. There is no `sizeof` operator yet: that comes in [Chapter 28](chapter-28.md).

**No pointer-to-integer casts.** You cannot convert a pointer to an integer to inspect its numeric address.

## What's Next

[Chapter 27](chapter-27.md) adds fixed-size arrays.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.
