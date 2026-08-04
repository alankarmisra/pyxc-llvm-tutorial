---
description: "Add sizeof and pointer casts so pyxc can call malloc and free: heap allocation with manual ownership."
---
# 21. pyxc: Heap Allocation

## Where We Are

[Chapter 20](chapter-20.md) gave me pointer arithmetic over fixed-size arrays, but those arrays have two limits I can't get around: their size has to be known at compile time, and they die the moment the function that declared them returns. Neither works for data whose size I only know at runtime, or that needs to outlive the function that created it. For that I need the heap, and the heap means calling `malloc` and `free`.

After this chapter:

```pyxc
extern def malloc(n: int64) -> ptr[int8]
extern def free(p: ptr[int8])
extern def printd(x: float64)

def main() -> int:
  var n: int64 = 5
  var raw: ptr[int8] = ptr[int8](malloc(n * sizeof(int64)))
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

`malloc` and `free` are just the C standard library functions. I don't need any new machinery to call them; `extern` already lets me call any C function. What I'm actually missing is two smaller things: a way to tell `malloc` how many bytes I want, and a way to tell pyxc what type the bytes it hands back should be treated as.

- **`sizeof(T)`** gives me the byte size of type `T` as a compile-time constant, so I can compute the right argument to `malloc`.
- **`ptr[T](expr)`** reinterprets a pointer of one type as a pointer of another, which I need because `malloc` only ever hands me `ptr[int8]` (raw bytes), and I want to treat them as, say, `ptr[int64]`.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-21
```

## One New Keyword

`sizeof` needs a token like every other keyword I've added:

```cpp
tok_sizeof = -37,
```

```cpp
{"sizeof", tok_sizeof}
```

## `sizeof(T)`: a Size I Don't Have to Compute Myself

I could compute a struct's size by hand: add up its fields, account for padding, remember that pointers are 8 bytes on a 64-bit target. But I already have all of that information, since I just built the LLVM type for every struct I've declared. Asking LLVM for the size directly is both less error-prone and correct across whatever target I eventually compile for.

`sizeof(T)` always produces an `int64`, no matter what `T` is:

```cpp
class SizeofExpressionNode : public ExpressionNode {
  ValueType TargetType;
  string TargetStructName;

public:
  SizeofExpressionNode(ValueType TargetType, const string &TargetStructName = "")
      : TargetType(TargetType), TargetStructName(TargetStructName) {
    setType(ValueType::Int64);
  }
  Value *codegen() override;
};
```

I call `setType(ValueType::Int64)` in the constructor rather than leaving it to be inferred later, since the result type never depends on `TargetType`: `sizeof(int8)` and `sizeof(Point)` are both `int64`.

Parsing it means reusing `ParseTypeToken`, the same function every other type annotation in pyxc goes through:

```cpp
static unique_ptr<ExpressionNode> ParseSizeofExpression() {
  getNextToken(); // eat 'sizeof'
  if (CurrentToken != '(')
    return LogError("Expected '(' after sizeof");
  getNextToken(); // eat '('
  string TargetStructName;
  ValueType TargetType = ParseTypeToken(&TargetStructName);
  if (TargetType == ValueType::Error)
    return nullptr;
  if (TargetType == ValueType::None)
    return LogError("Cannot take sizeof(None)");
  if (CurrentToken != ')')
    return LogError("Expected ')' after sizeof type");
  getNextToken(); // eat ')'
  return make_unique<SizeofExpressionNode>(TargetType, TargetStructName);
}
```

`sizeof(None)` is the one type I reject outright: `None` isn't a value at all, so asking for its size is a question that shouldn't have been asked. `ParsePrimary` routes `tok_sizeof` here:

```cpp
case tok_sizeof:
  return ParseSizeofExpression();
```

Codegen doesn't emit an instruction, since there's nothing to compute at runtime:

```cpp
Value *SizeofExpressionNode::codegen() {
  llvm::Type *Ty = LLVMTypeFor(TargetType, TargetStructName);
  if (!Ty)
    return LogErrorV("Invalid sizeof target type");
  uint64_t Bytes =
      TheModule->getDataLayout().getTypeAllocSize(Ty).getFixedValue();
  return ConstantInt::get(Type::getInt64Ty(*TheContext), Bytes);
}
```

`getTypeAllocSize` is LLVM's own answer to "how many bytes does one of these take up in an array," padding included, for whatever target I'm compiling for. I hand it a type and get back a number I fold directly into a constant. For a function that just returns `sizeof(int64)`, the generated IR is:

```llvm
define i64 @size_i64() {
entry:
  ret i64 8
}
```

No call, no load: the size was already known once code generation started, so it's just a literal by the time IR exists.

Sizes I get on a 64-bit target:

| Type | `sizeof` |
|------|---------|
| `int8` | 1 |
| `int32` | 4 |
| `int64` | 8 |
| `ptr[int8]` | 8 |
| `Point` (two `int` fields) | 16 |

Every pointer is 8 bytes here regardless of what it points to. LLVM's opaque pointer model means there's only one pointer representation at the IR level; pyxc is the one tracking what it points to, not LLVM.

## `ptr[T](expr)`: Reinterpreting a Pointer

Before this chapter, `ptr[int64](raw)` wasn't rejected by any type check, it was rejected by the parser before it got that far: `ParsePrimary` had no `case tok_ptr` at all, so a leading `ptr` in expression position was simply "unknown token when expecting an expression." I confirmed this against [Chapter 20](chapter-20.md)'s binary directly rather than guess at it.

So the fix isn't lifting a guard, it's adding a case, falling through to the exact same call every other cast target already uses:

```cpp
case tok_bool:
case tok_ptr:
  return ParseCastExpression();
case tok_sizeof:
  return ParseSizeofExpression();
```

Once `tok_ptr` is reachable, though, I do need one new guard, since `ParseCastExpression` will now happily see `Type == ValueType::Pointer` and I don't want to allow casting just anything to a pointer:

```cpp
if (Type == ValueType::Pointer && Expr->getType() != ValueType::Pointer)
  return LogError("Pointer casts require a pointer operand");
return make_unique<CastExpressionNode>(Type, std::move(Expr), TargetStructName);
```

I don't allow casting an integer to a pointer. There's no address I could hand it that pyxc could vouch for, and letting that through would just be a way to smuggle in undefined behavior with a friendlier syntax.

`CastExpressionNode` needs a `TargetStructName` now, for the same reason `SizeofExpressionNode` does: `ptr[Point]` and `ptr[int64]` are both `ValueType::Pointer`, so the pointee type has to travel separately:

```cpp
class CastExpressionNode : public ExpressionNode {
  ValueType TargetType;
  string TargetStructName;
  unique_ptr<ExpressionNode> Expr;

public:
  CastExpressionNode(ValueType TargetType, unique_ptr<ExpressionNode> Expr,
              const string &TargetStructName = "")
      : TargetType(TargetType), TargetStructName(TargetStructName),
        Expr(std::move(Expr)) {
    setType(TargetType, TargetStructName);
  }
  Value *codegen() override;
};
```

Without it, a cast to `ptr[int64]` would carry no pointee information at all, and anything downstream that indexes or reads through the result wouldn't know what it's pointing at.

Codegen for the pointer-to-pointer case is almost nothing, because at the LLVM level there's almost nothing to do:

```cpp
if (From == ValueType::Pointer && To == ValueType::Pointer)
  return Builder->CreateBitCast(V, LLVMTypeFor(ValueType::Pointer),
                                "ptrcast");
```

With opaque pointers, every pointer is the same IR type regardless of what it points to, so `CreateBitCast` between two of them doesn't actually emit an instruction: the value just passes through. I confirmed this by compiling a cast and reading the IR:

```llvm
%calltmp = call ptr @malloc(i64 8)
store ptr %calltmp, ptr %raw, align 8
%raw1 = load ptr, ptr %raw, align 8
store ptr %raw1, ptr %p, align 8
```

No `bitcast` anywhere. The cast's only real effect is at the pyxc level: the result is now typed `ptr[int64]` instead of `ptr[int8]`, so any GEP or load I generate from it afterward uses the right element type.

## Calling `malloc` and `free`

```pyxc
extern def malloc(n: int64) -> ptr[int8]
extern def free(p: ptr[int8])
```

Nothing about these declarations is special; any C function with compatible types can be called the same way. But there's a gap I ran into the first time I tried this: even though `malloc` is declared to return exactly `ptr[int8]`, assigning its result straight to a `var raw: ptr[int8]` fails to compile:

```pyxc
var raw: ptr[int8] = malloc(sizeof(Point))
```

```text
Error (Line 10, Column 45): Type mismatch in variable initialization
```

The pointee-type check I added in an earlier chapter compares the declared variable's pointee against the initializer's pointee, and a call to an `extern` function doesn't carry that pointee metadata through to its result the way a local expression does, even when the declared return type is the exact type I'm assigning to. I ran into the same error calling an extern function that returns `ptr[Point]`, so it isn't specific to `int8`; any pointer-returning `extern` call needs the same treatment. The fix is the cast I already have: wrap the call in an explicit same-type cast to set the pointee metadata myself.

```pyxc
var raw: ptr[int8] = ptr[int8](malloc(sizeof(Point)))
```

The full pattern for heap-allocating a single struct:

```pyxc
struct Point:
  x: int
  y: int

def main() -> int:
  var raw: ptr[int8] = ptr[int8](malloc(sizeof(Point)))
  var p: ptr[Point] = ptr[Point](raw)
  p[0].x = 77
  printd(float64(p[0].x))
  free(raw)
  return 0
```

`malloc` hands back raw bytes as `ptr[int8]`. `ptr[Point](raw)` tells pyxc to treat those same bytes as a `Point`, so `p[0].x` generates the right field offset. `free` gets the original `ptr[int8]` back. Passing `p` directly would be a type error, since `p` is `ptr[Point]`, not `ptr[int8]`.

## Build and Run

```bash
cd code/chapter-21
cmake -S . -B build && cmake --build build
```

## Try It

### `sizeof` of scalar types and a struct

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

```text
1.000000
4.000000
8.000000
8.000000
16.000000
```

### `malloc`, a pointer cast, and field access

```pyxc
extern def malloc(n: int64) -> ptr[int8]
extern def free(p: ptr[int8])
extern def printd(x: float64)

struct Point:
  x: int
  y: int

def main() -> int:
  var raw: ptr[int8] = ptr[int8](malloc(sizeof(Point)))
  var p: ptr[Point] = ptr[Point](raw)
  p[0].x = 77
  p[0].y = 33
  printd(float64(p[0].x))
  printd(float64(p[0].y))
  free(raw)
  return 0
```

```text
77.000000
33.000000
```

### `malloc` and pointer arithmetic: a heap array

```pyxc
extern def malloc(n: int64) -> ptr[int8]
extern def free(p: ptr[int8])
extern def printd(x: float64)

def main() -> int:
  var n: int64 = 5
  var raw: ptr[int8] = ptr[int8](malloc(n * sizeof(int64)))
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

```text
23.000000
```

### Inspecting the IR: `sizeof` really is just a constant

```bash
pyxc --emit llvm-ir -o out.ll program.pyxc
grep 'ret i64' out.ll
```

```llvm
ret i64 8
```

## Known Limitations

**No null check.** `malloc` can return null when the system is out of memory. I don't insert a null check; dereferencing a null pointer crashes silently.

**No bounds checking.** Accessing `p[n]` on a heap buffer of size `n` is an out-of-bounds write. I don't track buffer sizes anywhere.

**Manual ownership.** There's no destructor, no reference counting, no garbage collector. Forgetting to call `free` leaks memory; calling it twice or reading after `free` is undefined behavior: silently corrupted data, or a crash, with no diagnostic pointing at why.

**Pointer casts are pointer-only.** `ptr[T](expr)` requires `expr` to already be a pointer; I don't let an integer become a pointer through a cast. And casting a pointer-returning `extern` call result to its own declared type, as shown above, isn't optional: I have to do it every time, since the call itself doesn't carry pointee metadata.

## The Full Grammar

[pyxc.ebnf](https://github.com/alankarmisra/pyxc-llvm-tutorial/blob/main/code/chapter-21/pyxc.ebnf)

```ebnf
primary         = cast-expression | sizeof-expression | address-expression | name-expression | field-access | index-expression | number-expression | boolean-literal | parenthesized-expression ;
cast-expression        = cast-type "(" expression ")" ;
sizeof-expression      = "sizeof" "(" type ")" ;                       (* new *)
cast-type        = "int" | "int8" | "int16" | "int32" | "int64"
                | "float" | "float32" | "float64"
                | "bool" | pointer-type ;                              (* changed: pointer-type added *)
```

`sizeof-expression` is new. `cast-type` changed: `pointer-type` (`ptr[T]`) is now a valid cast target, where before it was rejected. Everything else (`type`, `field-access`, `index-expression`, and the rest of `primary`) is unchanged from [Chapter 20](chapter-20.md).

## What's Next

[Chapter 22](chapter-22.md) adds string literals: `"hello"` as a `ptr[int8]`, null-terminated global constants stored in the module, and escape sequences. With heap allocation and pointer casts already in place, I already know how to pass a `ptr[int8]` to a C function; string literals are the natural next step.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.
