---
description: "Add pointer arithmetic — ptr + int, ptr - int, ptr - ptr, and pointer comparisons — so you can walk memory with a pointer."
---
# 20. pyxc: Pointer Arithmetic

## Where We Are

[Chapter 19](chapter-19.md) gave us pointers: `addr` to take an address, `p[i]` to index, and pointer parameters so functions can modify the caller's data. What we could not do was walk a pointer forward or backward, or compare two pointers. The K&R summing-loop pattern — compute an end pointer, advance `p` until `p != end` — was out of reach.

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
  while p != end:
    total = total + p[0]
    p = p + 1
  printd(float64(total))  # 60.000000
  return 0
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-20
```

## Grammar

Pointer arithmetic reuses the existing binary operator infrastructure. No new tokens or keywords are needed — the type system drives the new behavior.

```ebnf
expr ::= ...
       | expr '+' expr   (* if one side is ptr[T] and other is int *)
       | expr '-' expr   (* ptr[T] - int, or ptr[T] - ptr[T] *)
       | expr '<' expr   (* pointer comparison *)
       | expr '>' expr
       | expr '==' expr
       | expr '!=' expr
       | expr '<=' expr
       | expr '>=' expr
```

The same tokens already existed. The change is that `GetBinaryResultType` now accepts pointer operands and returns pointer or integer types accordingly.

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

Pointer difference (`p - q`) yields an element count, not bytes. If `p` and `q` are both `ptr[int]` and they are 24 bytes apart, `p - q` is 3 — the number of `int`-sized steps between them.

Multiplication by a pointer is blocked. There is no sensible meaning for `ptr[T] * int` in terms of memory addresses.

## `GetBinaryResultType` Extended

`GetBinaryResultType` is the central function that decides what type a binary expression produces. Its signature is extended to carry struct-name information for both operands and to write the result struct name back:

```cpp
if (IsArithmeticOp(Op)) {
  if (Op != '*' &&
      ((L == ValueType::Pointer && IsIntType(R)) ||
       (R == ValueType::Pointer && IsIntType(L)))) {
    if (ResultStructName)
      *ResultStructName = (L == ValueType::Pointer) ? LStruct : RStruct;
    return ValueType::Pointer;
  }
  if (Op == '-' && L == ValueType::Pointer && R == ValueType::Pointer &&
      LStruct == RStruct)
    return ValueType::Int64;
  ...
}
if (IsComparisonOp(Op)) {
  if (L == ValueType::Pointer && R == ValueType::Pointer &&
      LStruct == RStruct)
    return ValueType::Bool;
  ...
}
```

For `ptr + int` and `int + ptr`, the result inherits the struct name (which encodes the pointee type) from whichever operand is the pointer. For `ptr - ptr`, no struct name is needed because the result is a plain `int64`. For pointer comparisons, the result is `bool`.

Mismatched pointer types (`ptr[int] - ptr[float64]`) fall through to the default error path because `LStruct != RStruct`.

## `BinaryExprAST` Constructor Extended

`BinaryExprAST` gains an optional `StructName` parameter so the pointer type of an arithmetic result can be carried through the AST:

```cpp
BinaryExprAST(char Op, unique_ptr<ExprAST> LHS, unique_ptr<ExprAST> RHS,
              ValueType Type = ValueType::Unknown,
              const string &StructName = "")
```

The constructor calls `setType(Type, StructName)`. Previously, only the parse-loop fallback path (for non-pointer results) needed to store a type on the node. Now the pointer arithmetic path explicitly constructs the node with both type and struct name set, then `continue`s to skip the old assignment statement.

## Parse Loop Change

The binary-operator parse loop now calls the extended `GetBinaryResultType` with both operands' struct names and a `ResultStructName` output. When the result is a pointer, the loop constructs the node with the full type information and continues:

```cpp
string ResultStructName;
ValueType ResultType = GetBinaryResultType(BinOp,
                                           LHS->getType(), LHS->getStructName(),
                                           RHS->getType(), RHS->getStructName(),
                                           &ResultStructName);
if (ResultType == ValueType::Pointer) {
  LHS = make_unique<BinaryExprAST>(BinOp, std::move(LHS), std::move(RHS),
                                   ResultType, ResultStructName);
  continue;
}
```

For non-pointer results the loop falls through to the existing assignment path unchanged.

## Codegen: `ptr + int` and `int + ptr`

Advancing a pointer by an integer uses `CreateGEP`. The integer index is widened to `i64` first:

```cpp
if (getType() == ValueType::Pointer) {
  Value *Ptr = nullptr;
  Value *Idx = nullptr;
  if (LType == ValueType::Pointer && IsIntType(RType) && Op == '+') {
    Ptr = L; Idx = EmitImplicitCast(R, RType, ValueType::Int64);
  } else if (RType == ValueType::Pointer && IsIntType(LType) && Op == '+') {
    Ptr = R; Idx = EmitImplicitCast(L, LType, ValueType::Int64);
  } else if (LType == ValueType::Pointer && IsIntType(RType) && Op == '-') {
    Ptr = L; Idx = EmitImplicitCast(R, RType, ValueType::Int64);
    if (Idx) Idx = Builder->CreateNeg(Idx, "negidx");
  }
  // ...
  return Builder->CreateGEP(ElemLLVM, Ptr, Idx, "ptrarith");
}
```

`DecodePointerType` extracts the element LLVM type from the result's encoded struct name. This is the type that GEP uses to compute its stride.

For `p + 1` where `p: ptr[int]`:

```llvm
%ptrload = load ptr, ptr %p
%ptrarith = getelementptr i64, ptr %ptrload, i64 1
```

## Codegen: `ptr - int`

Subtracting an integer is the same as adding its negation. The index is widened to `i64` and then negated with `CreateNeg` before being passed to GEP:

```llvm
%ptrload = load ptr, ptr %p
%negidx = neg i64 2
%ptrarith = getelementptr i64, ptr %ptrload, i64 %negidx
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
    LHS->getStructName() == RHS->getStructName()) {
  switch (Op) {
  case tok_eq:  return Builder->CreateICmpEQ(L, R, "cmptmp");
  case tok_neq: return Builder->CreateICmpNE(L, R, "cmptmp");
  case '<':     return Builder->CreateICmpULT(L, R, "cmptmp");
  case '>':     return Builder->CreateICmpUGT(L, R, "cmptmp");
  case tok_leq: return Builder->CreateICmpULE(L, R, "cmptmp");
  case tok_geq: return Builder->CreateICmpUGE(L, R, "cmptmp");
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
  printd(float64(p[0]))    # 20.000000 (b is next on the stack — layout-dependent)
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

### The K&R loop with `!= end`

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
  while p != end:
    total = total + p[0]
    p = p + 1
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

**`p - q` yields element count, not bytes.** If you need the byte distance, multiply by the element size manually. There is no `sizeof` operator yet — that comes in chapter 20.

**No pointer-to-integer casts.** You cannot convert a pointer to an integer to inspect its numeric address.

## What's Next

[Chapter 21](chapter-21.md) adds `malloc`, `free`, and `sizeof` — heap allocation built directly on the pointer arithmetic from this chapter. With `p + n`, `p - q`, and a way to allocate arbitrary memory, dynamic data structures become possible.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.
