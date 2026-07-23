---
description: "Add sizeof and pointer casts so pyxc can call malloc and free — heap allocation with manual ownership."
---
# 21. pyxc: Heap Allocation

## Where We Are

[Chapter 20](chapter-20.md) added fixed-size arrays — declared with a size known at compile time, allocated on the stack. That covers most local data, but sometimes you need memory whose size is only known at runtime, or whose lifetime must outlive the function that allocated it. For that you need the heap.

After this chapter:

```pyxc
extern def malloc(n: int64) -> ptr[int8]
extern def free(p: ptr[int8])
extern def printd(x: float64)

def main() -> int:
  var n: int64 = 5
  var raw: ptr[int8] = malloc(n * sizeof(int64))
  var p: ptr[int64] = ptr[int64](raw)
  p[0] = 5
  p[1] = 7
  p[2] = 9
  p[3] = 6
  p[4] = 8
  var q: ptr[int64] = p + 2
  printd(float64(q[0] + q[1] + q[2]))  # 23.000000
  free(raw)
  return 0
```

`malloc` and `free` are the C standard library functions. pyxc calls them directly through `extern` declarations. The two new pieces this chapter adds are:

- **`sizeof(T)`** — a compile-time constant giving the byte size of type `T`, so you can compute the right argument to `malloc`.
- **`ptr[T](expr)`** — a pointer cast that reinterprets a `ptr[S]` as a `ptr[T]`, carrying the new pointee type metadata through without emitting any IR instruction.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-21
```

## Grammar

```ebnf
sizeof-expr ::= 'sizeof' '(' type ')'

cast-expr   ::= type '(' expression ')'   (* extended: ptr[T](expr) now allowed *)
```

`sizeof` takes a type (not an expression) and returns an `int64` compile-time constant. `ptr[T](expr)` extends the existing cast expression to allow pointer-to-pointer reinterpretation; previously, casting to a pointer type was rejected.

`sizeof(None)` is rejected at parse time. `ptr[T](expr)` requires that `expr` is already a pointer — you cannot cast an integer to a pointer.

## One New Keyword

```cpp
tok_sizeof = -37,
```

Registered in the keyword map:

```cpp
{"sizeof", tok_sizeof}
```

## `sizeof`: Compile-Time Type Size

`sizeof(T)` evaluates to the number of bytes that an instance of type `T` occupies in memory, as determined by the target's data layout. It always has type `int64` and is computed entirely at compile time — no function call, no runtime overhead.

### `SizeofExprAST`

```cpp
class SizeofExprAST : public ExprAST {
  ValueType TargetType;
  string TargetStructName;
public:
  SizeofExprAST(ValueType TargetType, const string &TargetStructName = "")
      : TargetType(TargetType), TargetStructName(TargetStructName) {
    setType(ValueType::Int64);
  }
  Value *codegen() override;
};
```

The constructor immediately calls `setType(ValueType::Int64)` — the result type is always `int64`, regardless of what type was queried.

### Codegen

```cpp
Value *SizeofExprAST::codegen() {
  llvm::Type *Ty = LLVMTypeFor(TargetType, TargetStructName);
  uint64_t Bytes = TheModule->getDataLayout().getTypeAllocSize(Ty).getFixedValue();
  return ConstantInt::get(Type::getInt64Ty(*TheContext), Bytes);
}
```

`getTypeAllocSize` returns the number of bytes including any tail padding that the ABI requires between consecutive elements in an array. The result is emitted directly as a constant integer — not a call, not a load.

### IR example

For a function that returns `sizeof(int64)`:

```llvm
define i64 @size_i64() {
entry:
  ret i64 8
}
```

The compiler never emits a `sizeof` instruction. By the time code generation runs, the size has been computed and folded into a literal.

### Sizes on a 64-bit target

| Type | `sizeof` |
|------|---------|
| `int8` | 1 |
| `int32` | 4 |
| `int64` | 8 |
| `ptr[int8]` | 8 |
| `Point` (two `int` fields) | 16 |

All pointer types are 8 bytes on a 64-bit target regardless of what they point to — LLVM's opaque pointer model means there is only one pointer representation.

### Parsing

```cpp
static unique_ptr<ExprAST> ParseSizeofExpr() {
  getNextToken(); // eat 'sizeof'
  // expect '('
  getNextToken(); // eat '('
  string TargetStructName;
  ValueType TargetType = ParseTypeToken(&TargetStructName);
  if (TargetType == ValueType::None)
    return LogError("Cannot take sizeof(None)");
  // expect ')'
  getNextToken(); // eat ')'
  return make_unique<SizeofExprAST>(TargetType, TargetStructName);
}
```

`ParseTypeToken` is the same function used everywhere else a type annotation is parsed — `sizeof` reuses it directly. After parsing, `ParsePrimary` routes `tok_sizeof` here:

```cpp
case tok_sizeof:
  return ParseSizeofExpr();
```

## `ptr[T](expr)`: Pointer-to-Pointer Casts

Before this chapter, `ParseCastExpr` rejected any cast whose target type was a pointer or array:

```cpp
// old guard — now removed for the pointer case
if (Type == ValueType::Pointer || Type == ValueType::Array)
  return LogError("Cannot cast to pointer or array type");
```

Chapter 21 lifts the restriction for pointers. A cast like `ptr[int64](raw)` is now valid, provided the operand is itself a pointer:

```cpp
if (Type == ValueType::Pointer && Expr->getType() != ValueType::Pointer)
  return LogError("Pointer casts require a pointer operand");
return make_unique<CastExprAST>(Type, std::move(Expr), TargetStructName);
```

### `CastExprAST` extended

`CastExprAST`'s constructor now accepts a `TargetStructName` parameter and passes it to `setType`:

```cpp
CastExprAST(ValueType TargetType, unique_ptr<ExprAST> Expr,
            const string &TargetStructName = "")
    : Expr(std::move(Expr)) {
  setType(TargetType, TargetStructName);
}
```

Without this, a cast to `ptr[int64]` would carry no pointee information — the result would be typed as `ptr[?]` and subsequent indexing or field access would fail.

### Codegen: `EmitImplicitCast` Pointer→Pointer path

```cpp
if (From == ValueType::Pointer && To == ValueType::Pointer)
  return Builder->CreateBitCast(V, LLVMTypeFor(ValueType::Pointer), "ptrcast");
```

With LLVM's opaque pointer model, all pointers are the same IR type. `CreateBitCast` on two opaque `ptr` values emits no instruction at all — the IR value passes through unchanged. The cast's only real effect is at the pyxc level: the result node has type `ptr[int64]` instead of `ptr[int8]`, so downstream code generates correct GEPs and loads.

### IR example

For `ptr[int64](raw)` where `raw: ptr[int8]`:

```llvm
%ptrload = load ptr, ptr %raw
; no bitcast instruction emitted — opaque pointers are identical in IR
```

The pointer value is simply used with a different element type in any subsequent `getelementptr` or `load`/`store`.

## Routing in `ParsePrimary`

Two new cases:

```cpp
case tok_ptr:
  return ParseCastExpr();  // falls into existing cast path, now allows ptr[T](expr)

case tok_sizeof:
  return ParseSizeofExpr();
```

`tok_ptr` already appeared in `ParsePrimary` via the type-annotation path; now it also leads to `ParseCastExpr`, which handles the `ptr[T](expr)` form.

## Calling `malloc` and `free`

`malloc` and `free` are declared with `extern`, exactly like any other external C function:

```pyxc
extern def malloc(n: int64) -> ptr[int8]
extern def free(p: ptr[int8])
```

pyxc emits a standard LLVM `call` instruction, and the linker resolves it against the C runtime. There is nothing special about these declarations — any C function with compatible types can be called the same way.

The pattern for heap-allocating a single struct:

```pyxc
struct Point:
  x: int
  y: int

def main() -> int:
  var raw: ptr[int8] = malloc(sizeof(Point))
  var p: ptr[Point] = ptr[Point](raw)
  p[0].x = 77
  printd(float64(p[0].x))
  free(raw)
  return 0
```

`malloc` returns `ptr[int8]` — a raw byte pointer, same as in C. `ptr[Point](raw)` reinterprets it as a `ptr[Point]` so that `p[0].x` generates the right GEP. `free` receives the original `ptr[int8]`; passing `p` directly would be a type error because `p` is `ptr[Point]`.

## Build and Run

```bash
cd code/chapter-21
cmake -S . -B build && cmake --build build
```

## Try It

### sizeof of scalar types and a struct

```pyxc
extern def printd(x: float64)

struct Point:
  x: int
  y: int

def main() -> int:
  printd(float64(sizeof(int8)))
  printd(float64(sizeof(int32)))
  printd(float64(sizeof(int64)))
  printd(float64(sizeof(ptr[int8])))
  printd(float64(sizeof(Point)))
  return 0
```

```bash
1.000000
4.000000
8.000000
8.000000
16.000000
```

### malloc, pointer cast, and field access

```pyxc
extern def malloc(n: int64) -> ptr[int8]
extern def free(p: ptr[int8])
extern def printd(x: float64)

struct Point:
  x: int
  y: int

def main() -> int:
  var raw: ptr[int8] = malloc(sizeof(Point))
  var p: ptr[Point] = ptr[Point](raw)
  p[0].x = 77
  p[0].y = 33
  printd(float64(p[0].x))
  printd(float64(p[0].y))
  free(raw)
  return 0
```

```bash
77.000000
33.000000
```

### malloc and pointer arithmetic — a heap array

```pyxc
extern def malloc(n: int64) -> ptr[int8]
extern def free(p: ptr[int8])
extern def printd(x: float64)

def main() -> int:
  var n: int64 = 5
  var raw: ptr[int8] = malloc(n * sizeof(int64))
  var p: ptr[int64] = ptr[int64](raw)
  p[0] = 5
  p[1] = 7
  p[2] = 9
  p[3] = 6
  p[4] = 8
  var q: ptr[int64] = p + 2
  printd(float64(q[0] + q[1] + q[2]))
  free(raw)
  return 0
```

```bash
23.000000
```

### Inspect the IR: sizeof is a constant

Save the `sizeof(int64)` program and emit IR:

```bash
pyxc --emit llvm-ir -o out.ll program.pyxc
grep 'ret i64' out.ll
```

```llvm
ret i64 8
```

No call, no load — just a literal `8`.

## Known Limitations

**No null check.** `malloc` can return null when the system is out of memory. pyxc does not insert a null check; dereferencing a null pointer crashes silently.

**No bounds checking.** Accessing `p[n]` on a heap buffer of size `n` is an out-of-bounds write. pyxc does not track buffer sizes.

**Manual ownership.** There is no destructor, no reference counting, and no garbage collector. Forgetting to call `free` leaks memory; calling `free` twice or reading after `free` is undefined behavior — silently corrupted data or a crash.

**Pointer casts are pointer-only.** `ptr[T](expr)` requires `expr` to already be a pointer. You cannot cast an integer to a pointer (e.g., to use a raw address). Casting between pointer types is a reinterpretation at the pyxc metadata level only; LLVM sees no instruction.

## What's Next

[Chapter 22](chapter-22.md) adds string literals — `"hello"` as a `ptr[int8]`, null-terminated global constants stored in the module, and escape sequences. With heap allocation in place, the compiler already knows how to pass `ptr[int8]` to C functions; string literals are the natural next step.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.
