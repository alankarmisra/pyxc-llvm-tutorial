---
description: "Add struct types with field declarations, field read/write, and nested field access."
---
# 17. Pyxc: Structs

## Where We Are

[Chapter 16](chapter-16.md) gave Pyxc ten scalar types. Every value is still a single number — an int, a float, a bool. If you want to group a pair of coordinates and pass them around as one thing, you're out of luck.

This chapter adds structs. After this chapter:

```python
struct Point:
  x: int
  y: int

def distance_sq(p: Point) -> float64:
  return float64(p.x * p.x + p.y * p.y)

def main() -> int:
  var p: Point
  p.x = 3
  p.y = 4
  printd(distance_sq(p))  # 25.000000
  return 0
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-17
```

## Grammar

One new declaration and one new expression form:

```ebnf
struct-def    ::= 'struct' identifier ':' NEWLINE INDENT field+ DEDENT

field         ::= identifier ':' type NEWLINE

field-expr    ::= identifier ('.' identifier)+

field-assign  ::= field-expr '=' expression

type          ::= ...
               | identifier   (* struct name — must be declared above the point of use *)
```

`struct` is a top-level declaration, like `def` or `extern def`. It is not an expression. You cannot declare a struct inside a function.

Field access — `p.x`, `o.inner.value` — works both as an expression (read) and on the left side of `=` (write). Field access must start with a named variable. `make_point().x` is not supported yet.

## A Lurking Lexer Bug

Before anything else: a bug that was already there but only surfaced now. The number lexer entered the float-parsing path whenever it saw a standalone `.`:

```cpp
// Before — wrong
if (isdigit(LexerLastChar) || LexerLastChar == '.') {
```

That meant `p.x` would lex as: identifier `p`, then see `.` and enter the number-parsing path, find `x` instead of a digit, and produce garbage. Fine when `.` meant nothing. Fatal now that it separates a variable from its field.

The fix — only enter the float path when the character after `.` is actually a digit:

```cpp
// After — correct
if (isdigit(LexerLastChar) ||
    (LexerLastChar == '.' && isdigit(peek()))) {
```

`.5` still works as a float literal. `p.x` no longer gets eaten.

## The `struct` Keyword

```cpp
tok_struct = -34,
```

Registered in the keyword map alongside the other keywords:

```cpp
{"struct", tok_struct}
```

`ParseTypeToken` now recognises struct names as types:

```cpp
case tok_identifier: {
  string TyName = IdentifierStr;
  if (!StructTypes.count(TyName)) {
    LogError(("Unknown type '" + TyName + "'").c_str());
    return ValueType::Error;
  }
  getNextToken();
  if (StructName)
    *StructName = TyName;
  return ValueType::Struct;
}
```

`ValueType::Struct` is a new entry in the enum. Unlike the scalar types, a struct value is not self-describing — `ValueType::Struct` alone doesn't tell you which struct. You need the name alongside it to know the field layout. This is why `ParseTypeToken` now takes an optional `string *StructName` output parameter, and why every place that stores a `ValueType` for a struct also stores a `StructName` string next to it. There is a lot of that in this chapter.

## Tracking Struct Definitions at Parse Time

Two structs hold what the parser knows about a declared struct:

```cpp
struct StructFieldInfo {
  string Name;
  ValueType Type = ValueType::Error;
  string StructName;  // only set if Type == Struct
};

struct StructTypeInfo {
  string Name;
  vector<StructFieldInfo> Fields;
  std::map<string, size_t> FieldIndex;  // field name → index into Fields
};

static std::map<string, StructTypeInfo> StructTypes;
```

`StructTypes` is the global registry of all declared structs. It is populated at parse time and consulted at parse time — every field access and every struct type annotation looks the struct up here to validate it.

`FieldIndex` maps field name to position in `Fields`. It exists for two reasons: O(log n) lookup during field access parsing, and duplicate field detection during struct declaration.

## Parsing a Struct Definition

`ParseStructDefinition` is called when the top-level loop sees `tok_struct`. It reads the struct name, body, and field list, populating a `StructTypeInfo` and registering it:

```cpp
static bool ParseStructDefinition() {
  getNextToken(); // eat 'struct'
  string StructName = IdentifierStr;
  if (StructTypes.count(StructName)) {
    LogError(("Struct '" + StructName + "' is already defined").c_str());
    return false;
  }
  getNextToken(); // eat struct name
  // ... eat ':', newline, INDENT ...
  while (CurTok != tok_dedent && CurTok != tok_eof) {
    string FieldName = IdentifierStr;
    // ... eat ':', parse type ...
    if (Info.FieldIndex.count(FieldName)) {
      LogError(("Duplicate struct field '" + FieldName + "'").c_str());
      return false;
    }
    Info.FieldIndex[FieldName] = Info.Fields.size();
    Info.Fields.push_back({FieldName, FieldType, FieldStructName});
  }
  StructTypes[StructName] = std::move(Info);
  return true;
}
```

Struct bodies follow the same indentation rules as function bodies. Redefining a struct and declaring duplicate fields are both errors. Forward references are not supported — a struct must be declared before any use of it as a type.

## Two New AST Nodes

### `FieldExprAST`

A field read: `p.x`, `o.inner.value`.

```cpp
class FieldExprAST : public ExprAST {
  string BaseName;           // the variable at the root: "p" or "o"
  vector<string> FieldPath;  // the chain of field names: ["x"] or ["inner", "value"]
  ...
};
```

The type of the expression (set in the constructor) is the type of the last field in the path. `getLValueName()` returns `&BaseName` — used by assignment codegen to find the root pointer.

### `FieldAssignmentExprAST`

A field write: `p.x = 5`.

```cpp
class FieldAssignmentExprAST : public ExprAST {
  unique_ptr<FieldExprAST> LHS;
  unique_ptr<ExprAST> RHS;
  ...
};
```

`shouldPrintValue()` returns `false` — assignments produce no REPL output.

## Parsing Field Access

`ParseFieldAccessExpr` is called when the parser sees a `.` after an identifier that resolved to a struct variable. It walks the dot chain, validating each field against `StructTypes`:

```cpp
static unique_ptr<FieldExprAST> ParseFieldAccessExpr(
    string BaseName, ValueType BaseType, string BaseStructName) {
  vector<string> Path;
  ValueType CurType = BaseType;
  string CurStruct = BaseStructName;
  while (CurTok == '.') {
    getNextToken(); // eat '.'
    string Field = IdentifierStr;
    getNextToken(); // eat field name
    // look up Field in CurStruct's FieldIndex,
    // advance CurType and CurStruct to that field's type
    Path.push_back(Field);
  }
  return make_unique<FieldExprAST>(BaseName, Path, CurType, CurStruct);
}
```

Each step resolves the field type from `StructTypes`. By the time the loop exits, `CurType` and `CurStruct` describe the leaf field — the type the whole expression produces.

Field access on the left of `=` goes through the same `ParseFieldAccessExpr`, then into `ParseFieldAssignmentRHS`, which type-checks the RHS and wraps it in `FieldAssignmentExprAST`.

## Tracking Struct Names in Scope

Chapter 16 added `VarScopes: vector<map<string, ValueType>>` — a stack of maps from variable name to type. Struct variables need the struct name alongside `ValueType::Struct`, so a parallel stack is added:

```cpp
static vector<std::map<string, string>> VarStructScopes;
```

Every time a struct variable enters scope, both stacks are updated:

```cpp
static void DeclareVar(const string &Name, ValueType Type,
                       const string &StructName = "") {
  VarScopes.back()[Name] = Type;
  if (Type == ValueType::Struct)
    VarStructScopes.back()[Name] = StructName;
}
```

`LookupVarStructName` searches `VarStructScopes` innermost-first, then falls back to `GlobalVarStructTypes` for globals — mirroring how `LookupVarType` works:

```cpp
static string LookupVarStructName(const string &Name) {
  for (auto It = VarStructScopes.rbegin(); It != VarStructScopes.rend(); ++It) {
    auto Found = It->find(Name);
    if (Found != It->end())
      return Found->second;
  }
  auto GI = GlobalVarStructTypes.find(Name);
  if (GI != GlobalVarStructTypes.end())
    return GI->second;
  return "";
}
```

`PrototypeAST` also grows a `ReturnStructName` field, and the `pair<string, ValueType>` per argument from chapter 16 becomes an `ArgInfo` struct with `Name`, `Type`, and `StructName`. Same mechanics; just more to carry per argument.

## From Struct Name to LLVM Type

LLVM represents struct types as `StructType*` objects. `GetOrCreateLLVMStructType` converts a Pyxc struct name to the corresponding LLVM type, creating it on first use and caching the result:

```cpp
static std::map<string, StructType *> LLVMStructTypes;

static Type *GetOrCreateLLVMStructType(const string &StructName) {
  auto It = LLVMStructTypes.find(StructName);
  if (It != LLVMStructTypes.end())
    return It->second;

  auto *ST = StructType::create(*TheContext, "struct." + StructName);
  LLVMStructTypes[StructName] = ST;  // register before filling the body

  vector<Type *> FieldTys;
  for (const auto &Field : StructTypes[StructName].Fields)
    FieldTys.push_back(LLVMTypeFor(Field.Type, Field.StructName));
  ST->setBody(FieldTys, false);
  return ST;
}
```

Three things worth noting here.

First, the cache lookup is essential. LLVM creates a distinct `StructType` object each time you call `StructType::create` with the same name — it does not deduplicate them. Without the cache, two separate `alloca` instructions for the same struct would use two unrelated LLVM types with the same layout but different identities. Every load, store, and GEP that mixes them would fail.

Second, the type is registered in `LLVMStructTypes` before its body is filled. This is not an accident — it allows a struct to contain a pointer to itself without infinite recursion. A struct containing itself by value would be infinitely large, so that case doesn't come up in valid code.

Third, `setBody(FieldTys, false)` — the `false` means non-packed. Fields are laid out with natural alignment, the same as a C struct by default.

`LLVMTypeFor` dispatches to this function for `ValueType::Struct`:

```cpp
case ValueType::Struct:
  return GetOrCreateLLVMStructType(StructName);
```

## The IR Layout

For:

```python
struct Point:
  x: int
  y: int
```

The LLVM type, named with the `"struct."` prefix:

```llvm
%struct.Point = type { i64, i64 }
```

`int` is pointer-width (`i64` on a 64-bit host). A struct with a `float64` field:

```python
struct Circle:
  radius: float64
```

```llvm
%struct.Circle = type { double }
```

Fields appear in declaration order. LLVM inserts padding according to the target's data layout — it is not visible in the IR but is present in the machine code.

## Codegen: Getting a Field's Address

Reading or writing a field means computing a pointer to it first. `GetFieldAddress` does this by walking `FieldPath` one step at a time:

```cpp
static Value *GetFieldAddress(const string &BaseName,
                              const vector<string> &FieldPath, ...) {
  // find the base pointer — local alloca or global variable
  Value *Ptr = BasePtr;
  for (const auto &FieldName : FieldPath) {
    size_t Idx = StructTypes[CurStruct].FieldIndex[FieldName];
    Type *BaseLLVM = LLVMTypeFor(CurType, CurStruct);
    Ptr = Builder->CreateStructGEP(BaseLLVM, Ptr, Idx, "fieldptr");
    // advance CurType and CurStruct to this field's type
  }
  return Ptr;
}
```

`CreateStructGEP` emits a `getelementptr inbounds` for struct field access. One GEP per field step. For `p.x` on a `Point`:

```llvm
%fieldptr = getelementptr inbounds %struct.Point, ptr %p, i32 0, i32 0
```

For `o.inner.value` where `inner` is an `Inner`:

```llvm
%fieldptr  = getelementptr inbounds %struct.Outer, ptr %o, i32 0, i32 0
%fieldptr1 = getelementptr inbounds %struct.Inner, ptr %fieldptr, i32 0, i32 0
```

One GEP per field step, not one big multi-index GEP. Simpler codegen, same result.

## Codegen: Reading and Writing Fields

**Read:**

```cpp
Value *FieldExprAST::codegen() {
  Value *Ptr = GetFieldAddress(*getLValueName(), FieldPath, ...);
  return Builder->CreateLoad(LLVMTypeFor(LeafType, LeafStruct), Ptr, "fieldload");
}
```

Compute the pointer, load from it. For `p.x` where `x: int`:

```llvm
%fieldptr  = getelementptr inbounds %struct.Point, ptr %p, i32 0, i32 0
%fieldload = load i64, ptr %fieldptr
```

**Write:**

```cpp
Value *FieldAssignmentExprAST::codegen() {
  Value *Ptr = GetFieldAddress(*LHS->getLValueName(), LHS->getFieldPath(), ...);
  Value *Val = RHS->codegen();
  Val = EmitImplicitCast(Val, RHS->getType(), DestType);
  Builder->CreateStore(Val, Ptr);
  return Val;
}
```

Compute the pointer, codegen the RHS, implicit cast if needed, store. For `p.x = 5` where `x: int`:

```llvm
%fieldptr = getelementptr inbounds %struct.Point, ptr %p, i32 0, i32 0
store i64 5, ptr %fieldptr
```

The implicit cast rules from chapter 16 apply to field assignments. Assigning a `float64` to an `int` field is a type error. Assigning an `int8` to an `int` field widens silently.

## Struct Variables and Zero Initialization

`var p: Point` with no initializer allocates stack space and zero-initializes the struct:

```cpp
InitVal = ZeroConstant(VarType, VarStructName);
// ...
Builder->CreateStore(InitVal, Alloca);
```

`ZeroConstant` for a struct calls `Constant::getNullValue(LLVMTypeFor(Type, StructName))`, which produces a zero aggregate constant:

```llvm
%p = alloca %struct.Point
store %struct.Point zeroinitializer, ptr %p
```

There is no struct initializer syntax yet — `var p: Point = Point{x: 1, y: 2}` is not supported. Struct variables always start zeroed. Fields are then assigned individually.

## Structs Are Passed by Value

When a function takes a struct parameter, the caller passes a copy:

```python
struct Box:
  value: int

def clobber(b: Box) -> None:
  b.value = 0

def main() -> int:
  var b: Box
  b.value = 99
  clobber(b)
  # b.value is still 99 here
  return 0
```

The function signature in IR:

```llvm
define void @clobber(%struct.Box %b) {
entry:
  %b.addr = alloca %struct.Box
  store %struct.Box %b, ptr %b.addr
  %fieldptr = getelementptr inbounds %struct.Box, ptr %b.addr, i32 0, i32 0
  store i64 0, ptr %fieldptr
  ret void
}
```

`clobber` receives a copy of `b`. Writing to `b.value` inside `clobber` writes to that copy. The caller's struct is unchanged after the call. If you want a function to modify the caller's struct, you need a pointer — that's chapter 18.

## Global Struct Variables

Struct variables at global scope work the same as scalar globals:

```python
struct Counter:
  value: int

var g: Counter
```

Zero-initialized at program start:

```llvm
@g = global %struct.Counter zeroinitializer
```

Field reads and writes on globals go through the same `GetFieldAddress` path — it checks `NamedValues` for locals first, then falls back to `GetGlobalVariable`.

## Build and Run

```bash
cd code/chapter-17
cmake -S . -B build && cmake --build build
```

## Try It

### Basic field access

```python
struct Point:
  x: int
  y: int

extern def printd(x: float64)

def main() -> int:
  var p: Point
  p.x = 3
  p.y = 4
  printd(float64(p.x + p.y))
  return 0
```

```bash
7.000000
```

### Passing a struct to a function

```python
struct Point:
  x: int
  y: int

extern def printd(x: float64)

def sum_point(p: Point) -> int:
  return p.x + p.y

def main() -> int:
  var p: Point
  p.x = 5
  p.y = 7
  printd(float64(sum_point(p)))
  return 0
```

```bash
12.000000
```

### Nested field access

```python
struct Inner:
  value: int

struct Outer:
  inner: Inner

extern def printd(x: float64)

def main() -> int:
  var o: Outer
  o.inner.value = 9
  printd(float64(o.inner.value))
  return 0
```

```bash
9.000000
```

### Inspect the IR

```bash
pyxc --emit llvm-ir -o out.ll program.pyxc
grep 'struct\|getelementptr\|alloca' out.ll
```

## Known Limitations

**No struct initializer syntax.** `var p: Point = Point{x: 1, y: 2}` is not supported. Fields must be assigned individually after declaration.

**No struct-to-struct copy.** `var p2: Point = p1` is not supported. Whole-struct initialization from another variable isn't implemented yet.

**Field access must start with a named variable.** `make_point().x` is rejected — the base must be a variable in scope, not an expression.

**No pointer-to-struct.** Functions take structs by value. To share a struct across functions and have modifications be visible to the caller, you need a pointer — that's chapter 18.

## What's Next

[Chapter 18](chapter-18.md) adds pointers: `ptr[T]` as a type, `addr(x)` to take the address of a variable, and `p[i]` for pointer indexing. With pointers, you can pass a struct by reference and have functions modify the caller's data.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.
