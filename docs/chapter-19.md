---
description: "Add fixed-size arrays: T[N] type syntax, stack allocation, element indexing, array-to-pointer decay, and a[i].field for arrays of structs."
---
# 19. pyxc: Arrays

## Where We Are

[Chapter 18](chapter-18.md) gave us `ptr[T]` — a pointer to a single value somewhere in memory. That's enough for mutation through a function parameter, but not enough for a contiguous block of values. If you want ten integers sitting next to each other on the stack, you need an array.

After this chapter:

```python
extern def printd(x: float64)

def sum(arr: ptr[int], n: int) -> int:
  var total: int = 0
  for var i: int = 0, i < n, 1:
    total = total + arr[i]
  return total

def main() -> int:
  var nums: int[5]
  nums[0] = 1
  nums[1] = 2
  nums[2] = 3
  nums[3] = 4
  nums[4] = 5
  printd(float64(sum(nums, 5)))  # 15.000000
  return 0
```

`nums` is an array of five integers allocated on the stack. Passing it to `sum` — which takes a `ptr[int]` — works automatically. That last part, where an array silently becomes a pointer when you pass it to a function, is called array-to-pointer decay.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-19
```

## Grammar

```ebnf
type      ::= ...
            | base-type '[' integer ']'   (* fixed-size array *)

base-type ::= 'int' | 'int8' | 'int16' | 'int32' | 'int64'
            | 'float' | 'float32' | 'float64'
            | 'bool' | struct-name | 'ptr' '[' type ']'
```

The `[N]` suffix turns any base type into an array type. `int[4]` is an array of four `int` values. `Point[10]` is an array of ten `Point` structs. The array size must be a positive integer literal — no variables, no expressions, no zero.

Nested arrays (`int[2][3]`) and pointers to arrays (`ptr[int[4]]`) are rejected at parse time. Arrays of `None` are also rejected.

Indexing and assignment use the same `x[i]` and `x[i].field` syntax from chapter 18 — no new surface syntax required.

## `ValueType::Array`

```cpp
enum class ValueType {
  None, Int, Int8, Int16, Int32, Int64,
  Float, Float32, Float64, Bool,
  Struct, Array, Pointer,
  Error
};
```

`Array` sits between `Struct` and `Pointer`. Like `Pointer`, it is not self-describing — `ValueType::Array` alone does not tell you the element type or the count. Both travel alongside it in the `StructName` string field.

## The Encoding

The same `StructName` field that carries struct names and pointer metadata now carries array metadata:

```cpp
static string EncodeArrayType(ValueType ElemType, const string &ElemStructName,
                              uint64_t Count) {
  return std::to_string(static_cast<int>(ElemType)) + ":" + ElemStructName +
         ":" + std::to_string(Count);
}
```

`int[4]` encodes as `"1::4"` — element type `1` (Int), no struct name, count 4. `Point[10]` encodes as `"10:Point:10"` — element type `10` (Struct), struct name `"Point"`, count 10.

The decoder splits on the first and last `:`, which handles struct names that contain no colons:

```cpp
static bool DecodeArrayType(const string &Encoded, ValueType &ElemType,
                            string &ElemStructName, uint64_t &Count) {
  auto First = Encoded.find(':');
  auto Last  = Encoded.rfind(':');
  if (First == string::npos || Last == string::npos || First == Last)
    return false;
  int Raw = std::atoi(Encoded.substr(0, First).c_str());
  ElemType      = static_cast<ValueType>(Raw);
  ElemStructName = Encoded.substr(First + 1, Last - First - 1);
  return ParseUnsignedDecimal(Encoded.substr(Last + 1), Count) && Count > 0;
}
```

This is the same approach as `EncodePointerType`/`DecodePointerType`, with count tacked on as a third field.

## `ParseTypeToken` Extended

`ParseTypeToken` previously returned as soon as it identified the base type. Now it checks for a trailing `[` before returning, and if it finds one, it parses the array suffix:

```cpp
// After the switch that identifies the base type:
if (CurTok == '[') {
  if (BaseType == ValueType::None)
    return LogError("Arrays of None are not allowed"), ValueType::Error;
  if (BaseType == ValueType::Array)
    return LogError("Nested array types are not supported"), ValueType::Error;
  getNextToken(); // eat '['
  if (CurTok != tok_number || NumIsFloat)
    return LogError("Array size must be an integer literal"), ValueType::Error;
  uint64_t Count = 0;
  if (!ParseUnsignedDecimal(NumLiteralStr, Count))
    return LogError("Invalid array size"), ValueType::Error;
  if (Count == 0)
    return LogError("Array size must be > 0"), ValueType::Error;
  getNextToken(); // eat number
  if (CurTok != ']')
    return LogError("Expected ']' after array size"), ValueType::Error;
  getNextToken(); // eat ']'
  if (StructName)
    *StructName = EncodeArrayType(BaseType, BaseStructName, Count);
  return ValueType::Array;
}
```

The `tok_ptr` case now also sets `BaseType`/`BaseStructName` in the same switch and falls through to this check — so `ptr[int][4]` (an array of four pointers) is syntactically valid. `ptr[int[4]]` (a pointer to an array) is rejected inside the `tok_ptr` case before it ever reaches here.

## `LLVMTypeFor(Array)`: `[N x T]`

```cpp
case ValueType::Array: {
  ValueType ElemType; string ElemStructName; uint64_t Count;
  DecodeArrayType(StructName, ElemType, ElemStructName, Count);
  llvm::Type *ElemLLVM = LLVMTypeFor(ElemType, ElemStructName);
  return ArrayType::get(ElemLLVM, Count);
}
```

`int[4]` → `[4 x i64]`. `Point[2]` → `[2 x %struct.Point]`. The LLVM `ArrayType` knows its element type and count, which is exactly what `alloca` and `getelementptr` need.

## Stack Allocation

```cpp
AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, VarName, VarType, VarStructName);
Builder->CreateStore(ZeroConstant(VarType, VarStructName), Alloca);
```

`ZeroConstant` for arrays returns `ConstantAggregateZero::get(ArrayType)`, which LLVM renders as `zeroinitializer`. The resulting IR for `var a: int[4]`:

```llvm
%a = alloca [4 x i64]
store [4 x i64] zeroinitializer, ptr %a
```

All four slots start at zero. There is no per-element initialization loop — the store of `zeroinitializer` covers the whole aggregate in one instruction.

## Array-to-Pointer Decay

An array variable is not a pointer — it is a named stack object of type `[N x T]`. But the moment you use it in an expression, it decays to a pointer to its first element. This is what makes passing an array to a function that takes `ptr[T]` work.

The decay happens in `VariableExprAST::codegen`:

```cpp
auto DecayArray = [&](Value *Ptr) -> Value * {
  llvm::Type *ArrTy = LLVMTypeFor(ValueType::Array, getStructName());
  return Builder->CreateInBoundsGEP(
      ArrTy, Ptr,
      {ConstantInt::get(Type::getInt64Ty(*TheContext), 0),
       ConstantInt::get(Type::getInt64Ty(*TheContext), 0)},
      "arraydecay");
};

if (It != NamedValues.end() && It->second) {
  if (getType() == ValueType::Array)
    return DecayArray(It->second);
  // ...
}
```

The GEP `getelementptr inbounds [4 x i64], ptr %a, i64 0, i64 0` takes the address of the array and produces a pointer to element zero. The result is a `ptr` — the same opaque pointer type that `ptr[int]` produces. From here, indexing works identically for arrays and pointers.

For `var a: int[4]`:

```llvm
; accessing 'a' in an expression
%arraydecay = getelementptr inbounds [4 x i64], ptr %a, i64 0, i64 0
; %arraydecay is now a ptr — same as if you had loaded a ptr[int]
```

## Indexing an Array

`BuildIndexElementPtr` handles the base address computation for both arrays and pointers. For arrays, instead of loading a pointer from the variable, it computes the decay GEP:

```cpp
if (BaseType == ValueType::Pointer) {
  BasePtr = Builder->CreateLoad(LLVMTypeFor(ValueType::Pointer), BaseAddr, "ptrload");
} else if (BaseType == ValueType::Array) {
  Type *ArrayTy = LLVMTypeFor(ValueType::Array, BaseStructName);
  BasePtr = Builder->CreateInBoundsGEP(
      ArrayTy, BaseAddr,
      {ConstantInt::get(Type::getInt64Ty(*TheContext), 0),
       ConstantInt::get(Type::getInt64Ty(*TheContext), 0)},
      "arraybase");
}
// then index from BasePtr with the user-supplied index
```

Read: `a[2]` where `a: int[4]`:

```llvm
%arraybase = getelementptr inbounds [4 x i64], ptr %a, i64 0, i64 0
%elemptr   = getelementptr inbounds i64, ptr %arraybase, i64 2
%elemload  = load i64, ptr %elemptr
```

Write: `a[2] = 99`:

```llvm
%arraybase = getelementptr inbounds [4 x i64], ptr %a, i64 0, i64 0
%elemptr   = getelementptr inbounds i64, ptr %arraybase, i64 2
store i64 99, ptr %elemptr
```

The element GEP takes a `ptr` (the result of the decay) and steps `2 * sizeof(i64)` bytes forward. This is the same instruction sequence used for pointer indexing in chapter 18 — once the base pointer is established, the rest is identical.

## Array-to-Pointer Decay at Call Sites

When a function expects `ptr[T]` and you pass an array, the call site does the same decay:

```cpp
if (ParamType == ValueType::Pointer && ArgType == ValueType::Array) {
  if (!ArrayDecaysToPointerType(Args[i]->getStructName(),
                                Proto->getArgStructName(i)))
    return LogErrorV("Argument type mismatch");
  // no cast needed — the argument value is already a ptr from the decay GEP
}
```

`ArrayDecaysToPointerType` checks that the array element type matches the pointer's pointee type: a `float64[4]` passed to `ptr[int]` is rejected.

```cpp
static bool ArrayDecaysToPointerType(const string &ArrayInfo,
                                     const string &PointerInfo) {
  ValueType ArrElemType; string ArrElemStruct; uint64_t Count;
  DecodeArrayType(ArrayInfo, ArrElemType, ArrElemStruct, Count);
  ValueType PtrElemType; string PtrElemStruct;
  DecodePointerType(PointerInfo, PtrElemType, PtrElemStruct);
  return ArrElemType == PtrElemType && ArrElemStruct == PtrElemStruct;
}
```

The count is not checked — a `ptr[int]` parameter does not know or care how large the array was. That information is gone by the time the callee runs.

## `a[i].field`: Arrays of Structs

`a[i].field` uses the same `IndexedFieldExprAST` path introduced in chapter 18 for pointer-to-struct indexing. `BuildIndexElementPtr` now returns an element pointer for both arrays and pointers, so the field chain that follows is identical in both cases.

For `pts[1].x` where `pts: Point[2]`:

```llvm
%arraybase = getelementptr inbounds [2 x %struct.Point], ptr %pts, i64 0, i64 0
%elemptr   = getelementptr inbounds %struct.Point, ptr %arraybase, i64 1
%fieldptr  = getelementptr inbounds %struct.Point, ptr %elemptr, i32 0, i32 0
%fieldload = load i64, ptr %fieldptr
```

Three GEPs, no loads between them. The field assignment version replaces the final load with a store.

## Global Arrays

Global array variables work the same as global structs — `GlobalVariable` allocates the storage with `zeroinitializer`, and the ctor function zeros it again on startup:

```llvm
@g = global [2 x i64] zeroinitializer

define void @__pyxc_global_init() {
  store [2 x i64] zeroinitializer, ptr @g
}
```

Indexing and decay work identically for globals — `BuildIndexElementPtr` checks `GlobalVariable` if the name isn't in `NamedValues`.

## Build and Run

```bash
cd code/chapter-19
cmake -S . -B build && cmake --build build
```

## Try It

### Index a local array

```python
extern def printd(x: float64)

def main() -> int:
  var a: int[3]
  a[0] = 10
  a[1] = 20
  a[2] = 30
  printd(float64(a[1]))
  return 0
```

```
20.000000
```

### Pass an array to a function

```python
extern def printd(x: float64)

def first(p: ptr[int]) -> int:
  return p[0]

def main() -> int:
  var a: int[4]
  a[0] = 99
  printd(float64(first(a)))
  return 0
```

```
99.000000
```

The array decays to `ptr[int]` at the call site. Inside `first`, `p[0]` indexes the pointer exactly as chapter 18 showed.

### Array of structs

```python
extern def printd(x: float64)

struct Point:
  x: int
  y: int

def main() -> int:
  var pts: Point[2]
  pts[0].x = 3
  pts[0].y = 4
  pts[1].x = 10
  pts[1].y = 20
  printd(float64(pts[0].x + pts[1].x))
  return 0
```

```
13.000000
```

### Inspect the IR

```bash
./build/pyxc --emit llvm-ir -o out.ll program.pyxc
grep 'alloca\|getelementptr' out.ll
```

You should see the `alloca [N x T]` for the array and the two-step GEP sequence for each element access.

## Known Limitations

**No array literals.** You cannot write `var a: int[3] = [1, 2, 3]`. Elements must be assigned individually. Array literal syntax is planned for a future chapter.

**No bounds checking.** `a[100]` on a `int[4]` array is undefined behavior. It will happily read or write whatever is past the end of the array on the stack. There is no runtime check.

**No array-to-array assignment.** `var b: int[4] = a` is a type error. Arrays are not copyable. If you need a copy, write a loop.

**Size must be a compile-time constant.** `var a: int[n]` is rejected. The size is encoded in the type at parse time — variable-length arrays are not supported.

**No nested arrays.** `int[2][3]` is rejected. You can approximate a 2D array with a 1D array of structs, or with manual index arithmetic on a flat array.

**No pointer to array.** `ptr[int[4]]` is rejected. You can have `ptr[int]` (pointer to int, which can point into any array) or `int[4]` (the array itself). What you cannot have is a typed pointer that carries the array size with it.

**Element type is encoded in a string.** The `StructName` field on every `ExprAST` node carries array metadata as `"elemtype:elemstruct:count"`. Same tradeoff as pointer encoding — it works, it's not beautiful.

## What's Next

[Chapter 20](chapter-20.md) adds array literals: `[1, 2, 3]` as an initializer for `var` bindings, function arguments, and return values — so you no longer have to populate arrays one slot at a time.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.
