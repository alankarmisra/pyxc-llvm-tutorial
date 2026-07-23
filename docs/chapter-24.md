---
description: "Add fixed-size stack arrays: declare int[4], initialise with [1,2,3,4], and index with arr[i]. Arrays decay to pointers when passed to functions."
---
# 24. pyxc: Arrays

## Where We Are

[Chapter 23](chapter-23.md) added type aliases. The type system covers scalars, structs, and pointers, but there is no way to allocate a fixed-size sequence of values on the stack. After this chapter:

```pyxc
extern def printd(x: float64)

def main() -> int:
  var scores: int[4] = [10, 20, 30, 40]
  printd(float64(scores[2]))
  return 0
```

```
30.000000
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-24
```

## Grammar

This chapter extends `type` with an optional `arraysuffix` and adds the `arrayliteral` production to `primary`.

```ebnf
type        = basetype [ arraysuffix ] ;   -- changed
basetype    = builtintype | aliastype | structtype | pointertype ;  -- new
arraysuffix = "[" integer "]" ;            -- new
arrayliteral = "[" [ expression { "," expression } ] "]" ;  -- new
primary     = castexpr | sizeofexpr | addrexpr | arrayliteral | ...  -- changed
```

### Full Grammar

`code/chapter-24/pyxc.ebnf`

```ebnf
program         = [ eols ] [ top { eols top } ] [ eols ] ;
eols            = eol { eol } ;
top             = typealias | structdef | definition | decorateddef | external | toplevelexpr ;
typealias       = "type" identifier "=" type ;
structdef       = "struct" identifier ":" eols structblock ;
structblock     = indent fielddecl { eols fielddecl } dedent ;
fielddecl       = identifier ":" type ;
definition      = "def" prototype [ "->" type ] ":" ( simplestmt | eols block ) ;
decorateddef    = binarydecorator eols "def" binaryopprototype [ "->" type ] ":" ( simplestmt | eols block )
                | unarydecorator  eols "def" unaryopprototype  [ "->" type ] ":" ( simplestmt | eols block ) ;
binarydecorator = "@" "binary" "(" integer ")" ;
unarydecorator  = "@" "unary" ;
binaryopprototype = customopchar "(" typedparam "," typedparam ")" ;
unaryopprototype  = customopchar "(" typedparam ")" ;
external        = "extern" "def" prototype [ "->" type ] ;
toplevelexpr    = expression ;
prototype       = identifier "(" [ typedparam { "," typedparam } ] ")" ;
typedparam      = identifier ":" type ;
ifstmt          = "if" expression ":" suite
                [ eols "else" ":" suite ] ;
forstmt         = "for"
                  ( "var" identifier ":" type | identifier )
                  "=" expression "," expression "," expression ":" suite ;
varstmt         = "var" varbinding { "," varbinding } ;
assignstmt      = lvalue "=" expression ;
simplestmt      = returnstmt | varstmt | assignstmt | expression ;
compoundstmt    = ifstmt | forstmt ;
statement       = simplestmt | compoundstmt ;
suite           = simplestmt | compoundstmt | eols block ;
returnstmt      = "return" [ expression ] ;
block           = indent statement { eols statement } dedent ;
expression      = unaryexpr binoprhs ;
binoprhs        = { binaryop unaryexpr } ;
lvalue          = identifier | fieldaccess | indexexpr ;
varbinding      = identifier ":" type [ "=" expression ] ;
unaryexpr       = unaryop unaryexpr | primary ;
unaryop         = "-" | userdefunaryop ;
primary         = castexpr | sizeofexpr | addrexpr | arrayliteral | stringliteral | identifierexpr | fieldaccess | indexexpr | numberexpr | bool_literal | parenexpr ;
castexpr        = casttype "(" expression ")" ;
sizeofexpr      = "sizeof" "(" type ")" ;
addrexpr        = "addr" "(" lvalue ")" ;
identifierexpr  = identifier | callexpr ;
callexpr        = identifier "(" [ expression { "," expression } ] ")" ;
fieldaccess     = identifier "." identifier { "." identifier } ;
indexexpr       = identifier "[" expression "]" ;
numberexpr      = number ;
arrayliteral    = "[" [ expression { "," expression } ] "]" ;
stringliteral   = "\"" { ? any char except " and newline ? | escape } "\"" ;
escape          = "\\" ( "\\" | "\"" | "n" | "t" | "0" ) ;
parenexpr       = "(" expression ")" ;
binaryop        = builtinbinaryop | userdefbinaryop ;
indent          = INDENT ;
dedent          = DEDENT ;

builtinbinaryop = "+" | "-" | "*" | "<" | "<=" | ">" | ">=" | "==" | "!=" ;
userdefbinaryop = ? any opchar defined as a custom binary operator ? ;
userdefunaryop  = ? any opchar defined as a custom unary operator ? ;
customopchar    = ? any opchar that is not "-" or a builtinbinaryop,
                    and not already defined as a custom operator ? ;
opchar          = ? any single ASCII punctuation character ? ;
identifier      = (letter | "_") { letter | digit | "_" } ;
builtintype     = "int" | "int8" | "int16" | "int32" | "int64"
                | "float" | "float32" | "float64"
                | "bool" | "None" ;
aliastype       = identifier ;
structtype      = identifier ;
pointertype     = "ptr" "[" type "]" ;
type            = basetype [ arraysuffix ] ;
basetype        = builtintype | aliastype | structtype | pointertype ;
arraysuffix     = "[" integer "]" ;
casttype        = "int" | "int8" | "int16" | "int32" | "int64"
                | "float" | "float32" | "float64"
                | "bool" | pointertype ;
integer         = digit { digit } ;
number          = digit { digit } [ "." { digit } ]
                | "." digit { digit } ;
bool_literal    = "True" | "False" ;
letter          = "A".."Z" | "a".."z" ;
digit           = "0".."9" ;
eol             = "\r\n" | "\r" | "\n" ;
ws              = " " | "\t" ;
INDENT          = ? synthetic token emitted by lexer ? ;
DEDENT          = ? synthetic token emitted by lexer ? ;
```

## New Enum Value and AST Node

A single new `ValueType` entry covers all array types regardless of element type:

```cpp
enum class ValueType {
  // ...existing values...
  Array,
  // ...
};
```

The corresponding AST node stores the element list and carries the full array type info through the `ExprAST` base class:

```cpp
class ArrayLiteralExprAST : public ExprAST {
  vector<unique_ptr<ExprAST>> Elements;
public:
  ArrayLiteralExprAST(vector<unique_ptr<ExprAST>> Elements,
                      const string &ArrayTypeInfo)
      : Elements(std::move(Elements)) {
    setType(ValueType::Array, ArrayTypeInfo);
  }
  const vector<unique_ptr<ExprAST>> &getElements() const { return Elements; }
  Value *codegen() override;
};
```

`ArrayTypeInfo` is an encoded string (see Group 2) that packs the element type, optional element struct name, and element count into the single `structName` slot that `ExprAST` already provides.

## Type Encoding Helpers

The existing `(ValueType, structName)` pair that represents types throughout the compiler is extended for arrays with a third field — the element count. All three are serialised into one colon-separated string stored in the struct name slot:

```
"<ElemTypeInt>:<ElemStructName>:<Count>"
```

| Type         | Encoding       |
|--------------|----------------|
| `int[4]`     | `"1::4"`       |
| `float64[3]` | `"8::3"`       |
| `Point[2]`   | `"10:Point:2"` |

Three helpers manage this encoding:

**`EncodeArrayType`** builds the string:

```cpp
static string EncodeArrayType(ValueType ElemType, const string &ElemStructName,
                              uint64_t Count) {
  return std::to_string(static_cast<int>(ElemType)) + ":" + ElemStructName +
         ":" + std::to_string(Count);
}
```

**`DecodeArrayType`** splits it back out — first `:` separates the type integer, last `:` separates the count, and the middle portion is the struct name:

```cpp
static bool DecodeArrayType(const string &Encoded, ValueType &ElemType,
                            string &ElemStructName, uint64_t &Count);
```

**`ArrayDecaysToPointerType`** answers the question "can this array be passed where a `ptr[T]` is expected?" by decoding both the array encoding and the pointer encoding and comparing element types:

```cpp
static bool ArrayDecaysToPointerType(const string &ArrayInfo,
                                     const string &PointerInfo);
```

**`ParseUnsignedDecimal`** parses a digit string with overflow protection. It is used by both `DecodeArrayType` (to read the count field) and `ParseTypeToken` (to validate the size literal at parse time):

```cpp
static bool ParseUnsignedDecimal(const string &Text, uint64_t &Out) {
  if (Text.empty()) return false;
  uint64_t V = 0;
  for (char C : Text) {
    if (C < '0' || C > '9') return false;
    uint64_t D = static_cast<uint64_t>(C - '0');
    if (V > (std::numeric_limits<uint64_t>::max() - D) / 10) return false;
    V = V * 10 + D;
  }
  Out = V;
  return true;
}
```

## Parsing Array Types — `ParseTypeToken` Refactor

Previously, `ParseTypeToken` returned immediately after identifying the base type:

```cpp
case tok_int:
  return ValueType::Int;
```

That `return` makes suffix parsing impossible — once the function returns there is nowhere to check for `[`. This chapter refactors `ParseTypeToken` to collect the base type into local variables and then fall through to a suffix check:

```cpp
ValueType BaseType = ValueType::Error;
string BaseStructName;
switch (CurTok) {
  case tok_int:   BaseType = ValueType::Int;   break;
  case tok_float: BaseType = ValueType::Float; break;
  // ... all other scalar cases ...
  case tok_ptr:
    // parse ptr[T] as before, but store into BaseType/BaseStructName instead of returning
    BaseType = ValueType::Pointer;
    BaseStructName = EncodePointerType(PointeeType, PointeeStructName);
    break;
  case tok_identifier:
    BaseType = ValueType::Struct;
    BaseStructName = TyName;
    break;
}

// Now check for array suffix
if (CurTok == '[') {
  // error on None[] or nested arrays
  getNextToken(); // eat '['
  // parse integer size via ParseUnsignedDecimal
  // eat number, eat ']'
  if (StructName) *StructName = EncodeArrayType(BaseType, BaseStructName, Count);
  return ValueType::Array;
}

// No suffix — return the base type
if (StructName) *StructName = BaseStructName;
return BaseType;
```

`None[N]` and pointer-to-array (`ptr[int[4]]`) are rejected with explicit errors at parse time.

## Context-Driven Parsing — `ExpectedLiteralTypeGuard` and `ParseArrayLiteralExpr`

An array literal `[10, 20, 30, 40]` has no type of its own. The compiler must know what array type is expected to validate element types and count. This context is carried through a global pair:

```cpp
static ValueType ExpectedLiteralType;
static string    ExpectedLiteralStructName;  // new this chapter
```

**`ExpectedLiteralTypeGuard`** is extended to save and restore both fields. All callers that set a type context are updated to also pass the struct name:

```cpp
struct ExpectedLiteralTypeGuard {
  ValueType Saved;
  string    SavedStruct;
  ExpectedLiteralTypeGuard(ValueType Type, const string &StructName = "")
      : Saved(ExpectedLiteralType), SavedStruct(ExpectedLiteralStructName) {
    ExpectedLiteralType      = Type;
    ExpectedLiteralStructName = StructName;
  }
  ~ExpectedLiteralTypeGuard() {
    ExpectedLiteralType      = Saved;
    ExpectedLiteralStructName = SavedStruct;
  }
};
```

`ReturnTypeGuard` gains the same struct name field for the same reason — functions that return an array type need the full context propagated into the body.

**`ParseArrayLiteralExpr`** reads both globals at entry and errors immediately if the context is not an array type:

```cpp
static unique_ptr<ExprAST> ParseArrayLiteralExpr() {
  if (ExpectedLiteralType != ValueType::Array)
    return LogError("Array literal requires an expected array type");
  ValueType ElemType; string ElemStructName; uint64_t Count;
  DecodeArrayType(ExpectedLiteralStructName, ElemType, ElemStructName, Count);

  getNextToken(); // eat '['
  vector<unique_ptr<ExprAST>> Elements;
  while (CurTok != ']') {
    ExpectedLiteralTypeGuard Guard(ElemType, ElemStructName); // propagate into element expr
    auto E = ParseExpression();
    // type-check E against ElemType / ElemStructName
    Elements.push_back(std::move(E));
    if (CurTok == ',') getNextToken();
  }
  getNextToken(); // eat ']'
  if (Elements.size() != Count)
    return LogError("Array literal element count mismatch");
  return make_unique<ArrayLiteralExprAST>(std::move(Elements), ExpectedLiteralStructName);
}
```

The primary dispatch in `ParsePrimary` routes to this function when `CurTok == '['`. When the `var` statement parser reaches an initialiser, it sets `ExpectedLiteralTypeGuard(DeclType, DeclStructName)` before calling `ParseExpression`, so the type context is available by the time `ParseArrayLiteralExpr` runs.

## Codegen — `ArrayLiteralExprAST::codegen`

The literal is built in LLVM IR as a value of aggregate type, element-by-element, using `insertvalue`. There is no alloca here — this is pure register-level aggregate construction:

```cpp
Value *ArrayLiteralExprAST::codegen() {
  ValueType ElemType; string ElemStructName; uint64_t Count;
  DecodeArrayType(getStructName(), ElemType, ElemStructName, Count);

  auto *ArrTy = dyn_cast<ArrayType>(LLVMTypeFor(getType(), getStructName()));
  Value *Agg = UndefValue::get(ArrTy);  // start as undefined

  for (size_t I = 0; I < Elements.size(); ++I) {
    Value *Elem = Elements[I]->codegen();
    Elem = EmitImplicitCast(Elem, Elements[I]->getType(), ElemType);
    Agg = Builder->CreateInsertValue(Agg, Elem, {static_cast<unsigned>(I)},
                                     "arr.ins");
  }
  return Agg;  // SSA value of type [N x ElemTy]
}
```

`insertvalue` fills one slot of the aggregate per iteration. The returned SSA value has type `[4 x i64]` for `int[4]`. This is then stored into the alloca when assigned in a `var` statement:

```llvm
%scores = alloca [4 x i64]
store [4 x i64] %arr.ins.3, ptr %scores
```

## Codegen — Array Indexing and Variable Decay

**Indexing** (`scores[2]`) in `IndexExprAST::codegen` now handles `ValueType::Array` alongside `ValueType::Pointer`. For arrays, the element type and count are decoded from the array encoding, and a two-index GEP is emitted:

```llvm
%ptr = getelementptr inbounds [4 x i64], ptr %scores, i64 0, i64 2
%val = load i64, ptr %ptr
```

The first index (`i64 0`) steps through the alloca header to reach the array. The second index selects the element.

**Array decay** in `VariableExprAST::codegen` handles the case where an array variable is used in an expression context rather than indexed. Loading an array variable would produce the entire aggregate — which is only valid for `store`. To pass an array to a `ptr[T]` parameter, the compiler needs a pointer to its first element. A `DecayArray` lambda emits this GEP:

```cpp
auto DecayArray = [&](Value *BasePtr) -> Value * {
  if (getType() != ValueType::Array) return BasePtr;
  auto *ArrTy = LLVMTypeFor(getType(), getStructName());
  Value *Zero = ConstantInt::get(Type::getInt64Ty(*TheContext), 0);
  return Builder->CreateInBoundsGEP(ArrTy, BasePtr, {Zero, Zero}, "arraydecay");
};
```

This is applied for both local and global array variables. In function call codegen, the argument check path explicitly allows `ValueType::Array` where `ValueType::Pointer` is expected, delegating to `ArrayDecaysToPointerType` to confirm element-type compatibility.

## What Lands in the IR

```pyxc
def sum4(a: int[4]) -> int:
  return a[0] + a[1] + a[2] + a[3]
```

```llvm
define i64 @sum4([4 x i64] %a) {
entry:
  %a.addr = alloca [4 x i64]
  store [4 x i64] %a, ptr %a.addr
  %p0 = getelementptr inbounds [4 x i64], ptr %a.addr, i64 0, i64 0
  %v0 = load i64, ptr %p0
  ; ... and so on for indices 1, 2, 3
  %sum = add i64 %v0, ...
  ret i64 %sum
}
```

## Build and Run

```bash
cd code/chapter-24
cmake -S . -B build && cmake --build build
```

## Things Worth Knowing

**Size must be a literal.** `var buf: int[n]` is rejected — variable sizes are not supported. The element count must be a constant integer known at parse time.

**No nested arrays.** `int[4][2]` is not valid syntax. An array of arrays is not supported. Use a struct with multiple array fields for a 2-D layout.

**No heap arrays.** Arrays in this chapter live on the stack only. Heap allocation is done through `malloc` and `ptr[T]` from [chapter 20](chapter-21.md).

**Struct fields cannot be arrays.** Array fields in struct definitions are not yet supported. Struct fields use scalar or pointer types from earlier chapters.

**No pointer arithmetic on arrays.** Indexing works. Direct pointer arithmetic on an array variable does not — use `addr(arr[i])` to get a pointer to an element.

## What's Next

[Chapter 25](chapter-25.md) adds the `class` keyword — a named aggregate type that will support methods, constructors, and visibility in the chapters that follow.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.
