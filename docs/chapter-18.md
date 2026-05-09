---
description: "Add pointer types, addr() for taking addresses, and p[i] indexing — so functions can modify the caller's data."
---
# 18. pyxc: Pointers

## Where We Are

[Chapter 17](chapter-17.md) added structs, but with a catch: structs are passed by value. If you hand a struct to a function and the function modifies a field, the caller's copy is unchanged. That's fine for pure computations, and it's a deliberate design choice — but sometimes you actually want to modify the caller's data.

That's what pointers are for. After this chapter:

```python
struct Point:
  x: int
  y: int

def translate(p: ptr[Point], dx: int, dy: int) -> None:
  p[0].x = p[0].x + dx
  p[0].y = p[0].y + dy

def main() -> int:
  var pt: Point
  pt.x = 3
  pt.y = 4
  translate(addr(pt), 10, 20)
  printd(float64(pt.x))  # 13.000000
  printd(float64(pt.y))  # 24.000000
  return 0
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-18
```

## Grammar

```ebnf
type        ::= ...
              | 'ptr' '[' type ']'   (* pointer to type — no nesting allowed *)

addr-expr   ::= 'addr' '(' lvalue ')'

lvalue      ::= identifier ('.' identifier)*

index-expr  ::= lvalue '[' expression ']' ('.' identifier)*

index-assign ::= lvalue '[' expression ']' ('.' identifier)* '=' expression
```

`ptr[T]` is a type annotation only — you cannot construct one without `addr`. `addr` takes an lvalue (a named variable, optionally followed by field access) and returns a pointer to it. `p[i]` reads or writes the value at offset `i` from the pointer. `p[i].field` chains field access after indexing, for pointers to structs.

Nested pointer types (`ptr[ptr[int]]`) and pointers to `None` are rejected at parse time.

## Two New Keywords

```cpp
tok_ptr  = -35,
tok_addr = -36,
```

Registered in the keyword map:

```cpp
{"ptr", tok_ptr}, {"addr", tok_addr}
```

## `ValueType::Pointer`

`Pointer` is a new entry in the `ValueType` enum, after `Struct`. Unlike scalar types, a pointer value is not self-describing — `ValueType::Pointer` alone does not tell you what the pointer points to. You need the pointee type alongside it.

For scalar types and structs, the pointee information is carried in the `StructName` string field that already exists on every `ExprAST` node. For pointers, that same field is reused to carry an encoded string describing the pointee:

```cpp
static string EncodePointerType(ValueType PointeeType,
                                const string &PointeeStructName) {
  return std::to_string(static_cast<int>(PointeeType)) + ":" + PointeeStructName;
}

static bool DecodePointerType(const string &Encoded, ValueType &PointeeType,
                              string &PointeeStructName) {
  auto Pos = Encoded.find(':');
  // split on ':', parse the int as ValueType, rest is struct name
  ...
}
```

Every type in the compiler is described by two fields: a `ValueType` enum and a `StructName` string. For most types `StructName` is empty. For structs it holds the struct name. For pointers it holds a serialized description of the pointee, because `ValueType::Pointer` alone does not say what the pointer points to:

| Type | `ValueType` | `StructName` |
|---|---|---|
| `int`, `float64`, … | `Int`, `Float64`, … | `""` |
| `Point` (struct) | `Struct` | `"Point"` |
| `ptr[int]` | `Pointer` | `"1:"` |
| `ptr[Point]` | `Pointer` | `"10:Point"` |

The format is `"<ValueType int>:<struct name>"`. `ptr[int]` encodes as `"1:"` (`ValueType::Int` = 1, no struct name). `ptr[Point]` encodes as `"10:Point"` (`ValueType::Struct` = 10, struct name `"Point"`). The caller that created the pointer type is responsible for encoding; any site that needs the pointee type decodes it.

This reuses the existing type-tracking infrastructure without adding a new field to the base class. It is a tradeoff: the encoding is not beautiful, but it works and the surface is small.

## The LLVM Pointer Type

In LLVM IR, all pointer types are the same opaque type:

```cpp
case ValueType::Pointer:
  return PointerType::getUnqual(*TheContext);
```

`PointerType::getUnqual` produces the opaque `ptr` type — LLVM does not distinguish `ptr[int]` from `ptr[Point]` in the type system. The element type only appears in `getelementptr` and `load`/`store` instructions, not in the pointer type itself. This is LLVM's opaque pointer model, which has been the default since LLVM 15.

The zero value for a pointer is null:

```cpp
case ValueType::Pointer:
  return ConstantPointerNull::get(cast<PointerType>(LLVMTypeFor(ValueType::Pointer)));
```

`var p: ptr[int]` with no initializer starts as a null pointer.

## Parsing `ptr[T]`

`ParseTypeToken` handles the `tok_ptr` case:

```cpp
case tok_ptr: {
  getNextToken(); // eat 'ptr'
  // expect '['
  getNextToken(); // eat '['
  string PointeeStructName;
  ValueType PointeeType = ParseTypeToken(&PointeeStructName);
  // reject None and nested ptr
  getNextToken(); // eat ']'
  if (StructName)
    *StructName = EncodePointerType(PointeeType, PointeeStructName);
  return ValueType::Pointer;
}
```

The parsed pointee type is immediately encoded and written into the `StructName` output parameter. From this point on, the pointer's pointee information travels with it as an opaque string through `VarScopes`, `PrototypeAST::ArgInfo`, `VariableExprAST`, and every other place that stores a `ValueType` alongside a `StructName`.

## `addr`: Taking the Address of an Lvalue

`addr(x)` returns a pointer to `x`. `addr(p.x)` returns a pointer to the field `x` of struct `p`. `ParseAddrExpr` handles both:

```cpp
static unique_ptr<ExprAST> ParseAddrExpr() {
  getNextToken(); // eat 'addr'
  // expect '('
  getNextToken(); // eat '('
  // expect identifier — addr requires an lvalue
  string BaseName = IdentifierStr;
  getNextToken(); // eat identifier
  ValueType CurType = LookupVarType(BaseName);
  // walk optional field chain: addr(o.inner.value)
  vector<string> Path;
  while (CurTok == '.') {
    // validate each field, advance CurType and CurStruct
    Path.push_back(Field);
  }
  // expect ')'
  return make_unique<AddrExprAST>(BaseName, Path, CurType,
                                  EncodePointerType(CurType, CurStruct));
}
```

The resulting `AddrExprAST` has type `ValueType::Pointer` and its `StructName` holds the encoded pointee type.

`addr` only accepts a named variable, optionally with field access. Expressions like `addr(1 + 2)` are rejected immediately — the parser checks for `tok_identifier` right after the opening `(`.

### `AddrExprAST::codegen`

```cpp
Value *AddrExprAST::codegen() {
  if (FieldPath.empty()) {
    // addr(x) — return the alloca or global directly
    auto It = NamedValues.find(BaseName);
    if (It != NamedValues.end() && It->second)
      return It->second;
    if (auto *GV = GetGlobalVariable(BaseName))
      return GV;
    return LogErrorV("Unknown variable name");
  }
  // addr(p.x) — return the field pointer from GetFieldAddress
  Value *Ptr = GetFieldAddress(BaseName, FieldPath);
  return Ptr;
}
```

For a local variable, LLVM already represents it as an `alloca` — a pointer to its storage. `addr(x)` simply returns that pointer without any new instruction. For a struct field, `GetFieldAddress` (from chapter 17) computes and returns the GEP pointer for that field.

```llvm
; var x: int = 42
; var p: ptr[int] = addr(x)
%x = alloca i64
store i64 42, ptr %x
%p = alloca ptr
store ptr %x, ptr %p   ; addr(x) is just %x — the alloca itself
```

```llvm
; var pt: Point
; var px: ptr[int] = addr(pt.x)
%pt = alloca %struct.Point
...
%fieldptr = getelementptr inbounds %struct.Point, ptr %pt, i32 0, i32 0
%px = alloca ptr
store ptr %fieldptr, ptr %px
```

## `p[i]`: Pointer Indexing

`p[i]` computes the address at offset `i` from the pointer and loads from it. `p[i] = v` stores to it.

### Parsing

`ParseIndexExpr` is called from `ParseIdentifierExpr` whenever `[` follows a pointer-typed variable or field:

```cpp
static unique_ptr<ExprAST> ParseIndexExpr(string BaseName,
                                          vector<string> FieldPath,
                                          ValueType BaseType,
                                          const string &BaseStructName) {
  // reject if base is not a pointer
  getNextToken(); // eat '['
  auto Index = ParseExpression();
  // reject if index is not an integer type
  getNextToken(); // eat ']'
  // decode pointee type from BaseStructName
  return make_unique<IndexExprAST>(BaseName, FieldPath, Index, ElemType, ElemStruct);
}
```

The element type (what the pointer points to) is decoded from the encoded string in `BaseStructName`.

### `BuildIndexElementPtr`: the shared address computation

Both reads and writes need the element address. `BuildIndexElementPtr` computes it without loading:

```cpp
static Value *BuildIndexElementPtr(IndexExprAST *IdxExpr) {
  // load the pointer value from the base variable or field
  Value *BasePtr = LoadPointerValue(IdxExpr->getBaseName(),
                                    IdxExpr->getFieldPath(), ...);
  // widen index to i64 if needed
  Value *IdxVal = ...;
  // GEP: &base[i]
  return Builder->CreateInBoundsGEP(
      LLVMTypeFor(IdxExpr->getType(), IdxExpr->getStructName()),
      BasePtr, IdxVal, "elemptr");
}
```

The index is always widened to `i64` before the GEP — LLVM requires a consistent index type.

### Read codegen

```cpp
Value *IndexExprAST::codegen() {
  Value *ElemPtr = BuildIndexElementPtr(this);
  return Builder->CreateLoad(LLVMTypeFor(getType(), getStructName()),
                             ElemPtr, "elemload");
}
```

For `p[0]` where `p: ptr[int]`:

```llvm
%ptrload = load ptr, ptr %p        ; load the pointer value
%elemptr = getelementptr inbounds i64, ptr %ptrload, i64 0
%elemload = load i64, ptr %elemptr
```

For `p[1]`:

```llvm
%ptrload = load ptr, ptr %p
%elemptr = getelementptr inbounds i64, ptr %ptrload, i64 1
%elemload = load i64, ptr %elemptr
```

### Write codegen

```cpp
Value *IndexAssignmentExprAST::codegen() {
  Value *ElemPtr = BuildIndexElementPtr(LHS.get());
  Value *Val = RHS->codegen();
  Val = EmitImplicitCast(Val, RHS->getType(), getType());
  Builder->CreateStore(Val, ElemPtr);
  return Val;
}
```

For `p[0] = 99` where `p: ptr[int]`:

```llvm
%ptrload = load ptr, ptr %p
%elemptr = getelementptr inbounds i64, ptr %ptrload, i64 0
store i64 99, ptr %elemptr
```

The implicit cast rules from chapter 16 apply — assigning an integer to a `ptr[float64]` is a type error; assigning `int8` to `ptr[int]` widens.

## `p[i].field`: Field Access After Indexing

For pointers to structs, you can chain field access after the index: `p[0].x`. This requires a separate AST node because the base is an index expression, not a named variable.

### `IndexedFieldExprAST`

```cpp
class IndexedFieldExprAST : public ExprAST {
  unique_ptr<IndexExprAST> BaseIndex;  // the p[i] part
  vector<string> FieldPath;            // the field chain
  ...
};
```

`ParseIndexedFieldAccessExpr` is called when `ParseIdentifierExpr` sees a `.` after parsing an index expression. It walks the field chain exactly like `ParseFieldAccessExpr` from chapter 17:

```cpp
static unique_ptr<ExprAST>
ParseIndexedFieldAccessExpr(unique_ptr<IndexExprAST> BaseIndex) {
  // walk '.field' chain, validating each step against StructTypes
  return make_unique<IndexedFieldExprAST>(BaseIndex, Path, CurType, CurStruct);
}
```

### Codegen

```cpp
Value *IndexedFieldExprAST::codegen() {
  // get the element address without loading (BuildIndexElementPtr)
  Value *Ptr = BuildIndexElementPtr(BaseIndex.get());
  // walk field GEPs from that address
  for (const auto &FieldName : FieldPath) {
    Ptr = Builder->CreateStructGEP(BaseLLVM, Ptr, Idx, "fieldptr");
    // advance type
  }
  return Builder->CreateLoad(LLVMTypeFor(getType(), getStructName()), Ptr, "fieldload");
}
```

`BuildIndexElementPtr` computes the element address without loading the struct value — so the struct GEPs can chain directly from the element pointer.

For `p[0].x` where `p: ptr[Point]`:

```llvm
%ptrload = load ptr, ptr %p
%elemptr  = getelementptr inbounds %struct.Point, ptr %ptrload, i64 0
%fieldptr = getelementptr inbounds %struct.Point, ptr %elemptr, i32 0, i32 0
%fieldload = load i64, ptr %fieldptr
```

For `p[0].x = v` (write):

```llvm
%ptrload = load ptr, ptr %p
%elemptr  = getelementptr inbounds %struct.Point, ptr %ptrload, i64 0
%fieldptr = getelementptr inbounds %struct.Point, ptr %elemptr, i32 0, i32 0
store i64 %v, ptr %fieldptr
```

Two GEPs: one to reach element 0 of the array, one to reach field `x` of that element. No load between them — the pointer chains through.

## Mutation Through a Pointer Parameter

This is the payoff. A function that takes `ptr[T]` can modify the caller's data:

```python
def set_value(p: ptr[int], v: int) -> None:
  p[0] = v

def main() -> int:
  var x: int = 5
  set_value(addr(x), 100)
  # x is now 100
  return 0
```

```llvm
define void @set_value(ptr %p, i64 %v) {
entry:
  %p.addr = alloca ptr
  store ptr %p, ptr %p.addr
  %ptrload = load ptr, ptr %p.addr
  %elemptr = getelementptr inbounds i64, ptr %ptrload, i64 0
  store i64 %v, ptr %elemptr
  ret void
}
```

The pointer is passed by value (it's just an address), but the store through it writes to `x`'s alloca in the caller's stack frame. The caller sees the updated value.

Pointer arguments are type-checked: passing `ptr[float64]` where `ptr[int]` is expected is a type error.

## Parse Flow in `ParseIdentifierExpr`

The full sequence of what `ParseIdentifierExpr` handles, in order:

1. Parse the base identifier.
2. If `.` follows → parse field chain (`FieldExprAST`).
3. If `[` follows → parse index expression (`IndexExprAST`).
4. If `.` follows after step 3 → parse field chain on the index result (`IndexedFieldExprAST`).

This covers: `x`, `p.field`, `p[i]`, `p.field[i]`, `p[i].field`, `p[i].field.subfield`.

The statement parser has the same sequence to handle the left side of assignments.

## Build and Run

```bash
cd code/chapter-18
cmake -S . -B build && cmake --build build
```

## Try It

### Take an address, read through it

```python
extern def printd(x: float64)

def main() -> int:
  var x: int = 42
  var p: ptr[int] = addr(x)
  printd(float64(p[0]))
  return 0
```

```bash
42.000000
```

### Write through a pointer, see it in the caller

```python
extern def printd(x: float64)

def main() -> int:
  var x: int = 5
  var p: ptr[int] = addr(x)
  p[0] = 99
  printd(float64(x))
  return 0
```

```bash
99.000000
```

### Pass a struct by pointer

```python
extern def printd(x: float64)

struct Point:
  x: int
  y: int

def set_x(p: ptr[Point], v: int) -> None:
  p[0].x = v

def main() -> int:
  var pt: Point
  pt.x = 3
  set_x(addr(pt), 7)
  printd(float64(pt.x))
  return 0
```

```bash
7.000000
```

### Address of a struct field

```python
extern def printd(x: float64)

struct Point:
  x: int
  y: int

def main() -> int:
  var p: Point
  p.x = 11
  var px: ptr[int] = addr(p.x)
  printd(float64(px[0]))
  return 0
```

```bash
11.000000
```

### Inspect the IR

```bash
pyxc --emit llvm-ir -o out.ll program.pyxc
grep 'getelementptr\|load\|store' out.ll
```

## Known Limitations

**No pointer arithmetic.** `p + 1` is not supported — use `p[1]` to access adjacent elements.

**No nested pointers.** `ptr[ptr[int]]` is rejected at parse time.

**No pointer comparisons.** `p == nullptr` is not supported.

**No pointer-to-pointer casting.** You cannot reinterpret a `ptr[int]` as a `ptr[float64]`.

**Null pointer is silent.** `var p: ptr[int]` with no initializer is a null pointer. Dereferencing it crashes at runtime with no helpful error. Bounds checking and null safety are not implemented.

**Pointee type is encoded in a string.** The `StructName` field on `ExprAST` nodes doubles as pointer type metadata, stored as `"<ValueType int>:<struct name>"` (e.g. `"1:"` for `ptr[int]`, `"10:Point"` for `ptr[Point]`). It works but is not the cleanest representation — a dedicated field would be cleaner. This is a consequence of the single-AST-hierarchy design established in chapter 12.

## What's Next

[Chapter 19](chapter-19.md) adds fixed-size arrays: `T[N]`, stack allocation, indexing, and array-to-pointer decay. With arrays and pointers in place, you have the building blocks for the string and C interop chapter that follows.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.
