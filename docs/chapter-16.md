---
description: "Add a static type system: int, int8, int16, int32, int64, float, float32, float64, bool, and void (None). Parameters, variables, and return types are all explicitly annotated."
---
# 16. Pyxc: A Static Type System

## Where We Are

[Chapter 15](chapter-15.md) gave Pyxc debug info and proper optimisation pipelines. The language itself still has exactly one type: `double`. Every variable, parameter, return value, and literal is a 64-bit float, and the compiler never needs to ask "what type is this?".

This chapter adds a real type system. After this chapter:

```python
def add(a: int32, b: int32) -> int32:
    return a + b

var counter: int = 0
var ratio: float64 = 3.14

def classify(x: float64) -> bool:
    return x > 0.0

if classify(1.5):
    printd(float64(counter))
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-16
```

## Grammar

The grammar gains type annotations throughout:

```ebnf
type        ::= 'int' | 'int8' | 'int16' | 'int32' | 'int64'
              | 'float' | 'float32' | 'float64'
              | 'bool' | 'None'

param       ::= identifier ':' type
paramlist   ::= param (',' param)*

prototype   ::= identifier '(' paramlist? ')'
return-ann  ::= ('->' type)?

definition  ::= 'def' prototype return-ann ':' suite
extern-def  ::= 'extern' 'def' prototype return-ann

var-stmt    ::= 'var' identifier ':' type ('=' expression)?
              | 'var' identifier ':' type (',' identifier ':' type)*

for-stmt    ::= 'for' identifier ':' type '=' expression ','
                expression ',' expression ':' suite

castexpr    ::= type '(' expression ')'

bool-literal ::= 'True' | 'False'
```

Key changes from chapter 15:

- Every parameter requires `: type`.
- `var` declarations require `: type`, optionally followed by `= expression`.
- `for` loop variables require `: type` between the name and `=`.
- A function carries `-> type` between `)` and `:`. If absent, `def` defaults to `None` (void); `extern def` and operator defs default to `float64`.
- `None` is the void return type annotation; it cannot be used as a variable or parameter type.
- `True` and `False` are new boolean literal keywords.

## The Design

### The ValueType Enum

Every value now has a type, represented as a `ValueType` enum:

```cpp
enum class ValueType {
  None,    // void — no return value
  Int,     // target-machine default integer (pointer-sized: 32 or 64 bits)
  Int8,    // 8-bit signed integer
  Int16,   // 16-bit signed integer
  Int32,   // 32-bit signed integer
  Int64,   // 64-bit signed integer
  Float,   // 64-bit double (the 'float' keyword)
  Float32, // 32-bit float
  Float64, // 64-bit double (the 'float64' keyword)
  Bool,    // 1-bit boolean (i1)
  Error    // sentinel — parse/type error
};
```

Each `ValueType` maps to a fixed LLVM IR type:

| ValueType | Keyword | LLVM IR type | Notes |
|-----------|---------|--------------|-------|
| `Int` | `int` | `i64` / `i32` | pointer-width — host-dependent |
| `Int8` | `int8` | `i8` | always 8-bit |
| `Int16` | `int16` | `i16` | always 16-bit |
| `Int32` | `int32` | `i32` | always 32-bit |
| `Int64` | `int64` | `i64` | always 64-bit |
| `Float` | `float` | `double` | same IR type as Float64 |
| `Float32` | `float32` | `float` | 32-bit IEEE single |
| `Float64` | `float64` | `double` | 64-bit IEEE double |
| `Bool` | `bool` | `i1` | 1-bit integer |
| `None` | `None` | `void` | no-value return |

`Int` (no size suffix) maps to the pointer-width integer on the target machine. On a 64-bit host it is `i64`; on a 32-bit host it is `i32`.

`Int32` is always `i32` regardless of target. `int32(x)` is a reliable cross-platform 32-bit integer; `int` is a platform-default convenience.

`Float` and `Float64` are **distinct enum values but compile to the same IR type** — both produce `double` from `LLVMTypeFor`. The `float` keyword gives you `ValueType::Float`; the `float64` keyword gives you `ValueType::Float64`. They are interchangeable everywhere: assignment, binary operations, function arguments, and debug info all treat them as the same 64-bit float. The distinction exists at the token and enum level only so that error messages and `TypeName()` can round-trip the exact keyword the programmer wrote.

`None` is the return type of functions that produce no value. It corresponds to LLVM's `void` and cannot be used as a variable or parameter type.

`Error` is a sentinel returned by type helpers when something is wrong. It propagates errors without needing `Optional`.

### Implicit Conversions

The type system is strict. The only implicit conversions allowed are:

1. **Same type → always allowed.**
2. **Float ↔ Float64** — the two 64-bit float spellings are freely interchangeable. No instruction is emitted since they share the same IR type.
3. **Integer widening** — smaller fixed-size integers can be assigned to larger ones: `Int8 → Int16 → Int32 → Int64`. Also, `Int` (pointer-width) can widen to `Int64`. Emits `sext`.
4. **Any integer type → any float type** — an integer can be silently converted to `Float`, `Float32`, or `Float64`. Emits `sitofp`.

Everything else requires an explicit cast. In particular:
- Narrowing (e.g., `int64` to `int32`) always requires an explicit cast.
- `Bool` is not implicitly assignable from any other type.
- `Int` does not widen to `Int32` or smaller fixed-size types (use an explicit cast).

In IR, the three implicit cases look like:

```llvm
; Integer widening: int8 → int16
%wide = sext i8 %x to i16

; Integer → float: int32 → float64
%asf = sitofp i32 %n to double

; Float ↔ Float64: no instruction — same IR type double
; the LLVM Value* is used directly
```

## New Tokens

Three groups of tokens are added.

### The `->` Arrow

```cpp
tok_arrow = -12, // ->
```

The lexer detects `->` as a single token:

```cpp
if (LexerLastChar == '-') {
  int Tok = (peek() == '>') ? (advance(), tok_arrow) : '-';
  LexerLastChar = advance();
  return Tok;
}
```

This must appear before the generic `-` path so that `->` is never split into two tokens.

### Type Keywords

```cpp
tok_int = -20,
tok_int8 = -23,  tok_int16 = -24, tok_int32 = -25, tok_int64 = -26,
tok_float = -27, tok_float32 = -28, tok_float64 = -29,
tok_bool = -30,
tok_none = -31,
```

Registered in the keyword map:

```cpp
{"int", tok_int},         {"int8", tok_int8},       {"int16", tok_int16},
{"int32", tok_int32},     {"int64", tok_int64},
{"float", tok_float},     {"float32", tok_float32},  {"float64", tok_float64},
{"bool", tok_bool},
{"None", tok_none}
```

`float` and `float64` are **separate tokens** — `tok_float = -27` and `tok_float64 = -29`. `ParseTypeToken` maps `tok_float → ValueType::Float` and `tok_float64 → ValueType::Float64`. Both compile to `double`, but the distinction is preserved through the entire pipeline until IR emission.

### Boolean Literal Keywords

```cpp
tok_true = -32,
tok_false = -33,
```

```cpp
{"True", tok_true}, {"False", tok_false}
```

`True` and `False` (capital first letter, matching Python) are lexed as keywords, not identifiers.

## Numeric Literal Types

Before chapter 16, every number literal stored a `double`. Now literals have proper types. The lexer sets a flag:

```cpp
NumIsFloat = NumStr.find('.') != string::npos;
```

`ParseNumberExpr` uses `APInt` or `APFloat` depending on this flag:

```cpp
static unique_ptr<ExprAST> ParseNumberExpr() {
  ValueType Type = NumIsFloat ? ValueType::Float64 : ValueType::Int;
  if (NumIsFloat) {
    if (IsFloatType(ExpectedLiteralType))
      Type = ExpectedLiteralType;
    const fltSemantics &Semantics =
        (Type == ValueType::Float32) ? APFloat::IEEEsingle()
                                     : APFloat::IEEEdouble();
    APFloat Val(Semantics, NumLiteralStr);
    return make_unique<NumberExprAST>(Val, Type);
  } else {
    if (IsIntType(ExpectedLiteralType))
      Type = ExpectedLiteralType;
    unsigned Bits = LLVMTypeFor(Type)->getIntegerBitWidth();
    // ...parse APInt and range-check...
    return make_unique<NumberExprAST>(Val, Type);
  }
}
```

`NumberExprAST` stores the literal with full precision:

```cpp
class NumberExprAST : public ExprAST {
  bool IsIntLiteral;
  APInt IntVal;
  APFloat FloatVal;
public:
  NumberExprAST(APInt Val, ValueType Type)
      : IsIntLiteral(true), IntVal(std::move(Val)), FloatVal(0.0) { setType(Type); }
  NumberExprAST(APFloat Val, ValueType Type)
      : IsIntLiteral(false), IntVal(1, 0), FloatVal(std::move(Val)) { setType(Type); }
};
```

The IR constants each produces:

```llvm
; 42   — integer literal, no '.', defaults to Int (i64 on 64-bit host)
i64 42

; 42.0 — float literal, has '.', defaults to Float64
double 4.200000e+01

; var x: int32 = 5   — context sets int32, literal is parsed as i32 directly
i32 5

; var x: float32 = 1.5  — context sets float32, literal parsed at single precision
float 1.500000e+00

; var b: int8 = 200   — out of range for i8, parse error before any IR is emitted
; Error: Integer literal out of range for type
```

`3.14` in a `var x: float32` context becomes a `Float32` literal parsed with IEEE single precision — so `3.14` stores the nearest `float` value, not the nearest `double`. Without this, the parse would produce a `double` constant and then require an `fptrunc` to `float` at the store — which is not an implicit conversion and would be a type error.

## Context-Sensitive Literal Types

`ParseNumberExpr` consults a global `ExpectedLiteralType`:

```cpp
static ValueType ExpectedLiteralType = ValueType::Error;

struct ExpectedLiteralTypeGuard {
  ValueType Saved;
  ExpectedLiteralTypeGuard(ValueType Type) : Saved(ExpectedLiteralType) {
    ExpectedLiteralType = Type;
  }
  ~ExpectedLiteralTypeGuard() { ExpectedLiteralType = Saved; }
};
```

Three sites install a guard:

- **`var` initializers.** `ParseVarStmt` installs the declared type so `var x: int32 = 5` parses `5` as `int32` directly.
- **`return` expressions.** `ParseReturn` installs the enclosing function's return type.
- **Function call arguments.** `ParseIdentifierExpr` (the call parser) installs each parameter's declared type before parsing the corresponding argument expression.

The effect on the IR is that no redundant cast instruction appears:

```llvm
; WITHOUT context guard: var x: float32 = 1.5
; 1.5 parsed as double, then an fptrunc would be needed — but that's not implicit,
; so this would be a type error.

; WITH context guard: var x: float32 = 1.5
; 1.5 parsed as float directly — clean alloca + store, no cast
%x = alloca float
store float 1.500000e+00, ptr %x
```

The guard is a scoped RAII object: when it goes out of scope, `ExpectedLiteralType` reverts to whatever it was before, so nested expressions are not affected.

## Boolean Literals

`True` and `False` are parsed in `ParsePrimary`:

```cpp
case tok_true:
  getNextToken();
  return make_unique<BoolExprAST>(true);
case tok_false:
  getNextToken();
  return make_unique<BoolExprAST>(false);
```

`BoolExprAST` is a new AST class:

```cpp
class BoolExprAST : public ExprAST {
  bool Val;
public:
  BoolExprAST(bool Val) : Val(Val) { setType(ValueType::Bool); }
  Value *codegen() override;
};
```

`BoolExprAST::codegen` emits an `i1` constant:

```cpp
Value *BoolExprAST::codegen() {
  return ConstantInt::get(Type::getInt1Ty(*TheContext), Val ? 1 : 0);
}
```

```llvm
; True
i1 true

; False
i1 false
```

`Bool` is a distinct type. It is not the result of a comparison widened to an integer — it stays `i1` throughout. Comparisons also return `Bool` / `i1` in chapter 16.

## ParseTypeToken and ParseOptionalReturnType

`ParseTypeToken` consumes the current token if it is a type keyword and returns the corresponding `ValueType`:

```cpp
static ValueType ParseTypeToken() {
  switch (CurTok) {
  case tok_int:     getNextToken(); return ValueType::Int;
  case tok_int8:    getNextToken(); return ValueType::Int8;
  case tok_int16:   getNextToken(); return ValueType::Int16;
  case tok_int32:   getNextToken(); return ValueType::Int32;
  case tok_int64:   getNextToken(); return ValueType::Int64;
  case tok_float:   getNextToken(); return ValueType::Float;
  case tok_float32: getNextToken(); return ValueType::Float32;
  case tok_float64: getNextToken(); return ValueType::Float64;
  case tok_bool:    getNextToken(); return ValueType::Bool;
  case tok_none:    getNextToken(); return ValueType::None;
  default:
    LogError("Expected a type");
    return ValueType::Error;
  }
}
```

`tok_float` and `tok_float64` produce distinct `ValueType` values even though both compile to `double`.

`ParseOptionalReturnType` wraps it with a default:

```cpp
static ValueType ParseOptionalReturnType(
    ValueType DefaultType = ValueType::None) {
  if (CurTok != tok_arrow)
    return DefaultType;
  getNextToken(); // eat '->'
  return ParseTypeToken();
}
```

The `DefaultType` parameter is the key design decision:

- `ParseDefinition` calls `ParseOptionalReturnType(ValueType::None)` — unannotated `def` is void.
- `ParseExtern` and operator parsers call `ParseOptionalReturnType()` (default `Float64`) — extern declarations and operator overloads default to `float64` because they are typically C library functions or mathematical operators. `ParseDecoratedDef` calls `ParseOptionalReturnType()` and immediately calls `Proto->setReturnType(RetTy)`, so any explicit `->` annotation overrides that placeholder.

`ValueType::None` also serves as a placeholder type for all statement-like AST nodes that produce no meaningful value: `ReturnExprAST`, `BlockExprAST`, `ForExprAST`, `IfStmtAST`, and `VarStmtAST` all call `setType(ValueType::None)` in their constructors, and all override `shouldPrintValue()` to return `false`. This is the same pattern as the single-hierarchy AST design noted in chapter 12 — in a clean Stmt/Expr split, statements would carry no type at all. Here, `None` is the convention that means "this node is a statement; its type is not meaningful."

## ParsePrototype: Typed Parameters

Chapter 15's prototype parser accepted bare identifiers:

```cpp
// Chapter 15
while (getNextToken() == tok_identifier)
  ArgNames.push_back(IdentifierStr);
```

Chapter 16 requires `name : type` for every parameter:

```cpp
// Chapter 16
vector<pair<string, ValueType>> ArgNames;
getNextToken(); // eat '('
if (CurTok != ')') {
  while (true) {
    string ArgName = IdentifierStr;
    getNextToken(); // eat identifier
    if (CurTok != ':')
      return LogErrorP("Parameters require type annotations (e.g., ': int32')");
    getNextToken(); // eat ':'
    ValueType ArgTy = ParseTypeToken();
    if (ArgTy == ValueType::None)
      return LogErrorP("Parameters cannot have None type");
    ArgNames.push_back({ArgName, ArgTy});
    if (CurTok == ')') break;
    if (CurTok != ',') return LogErrorP("Expected ')' or ','");
    getNextToken(); // eat ','
  }
}
```

`PrototypeAST` now stores `vector<pair<string, ValueType>>` and a `ReturnType` field, with helpers `getArgType(i)`, `getReturnType()`, `setReturnType()`, and `clone()`.

The same function signature in chapter 15 vs chapter 16:

```llvm
; Chapter 15
define double @add(double %a, double %b) { ... }

; Chapter 16
define i32 @add(i32 %a, i32 %b) { ... }
define i1 @classify(double %x) { ... }
define void @greet() { ... }
```

Every parameter type and every return type now appears literally in the IR rather than being uniformly `double`.

## VarScopes: From Set to Map

Chapter 15 tracked which variable names were in scope using a `set<string>`:

```cpp
// Chapter 15
static vector<set<string>> VarScopes;
```

Chapter 16 upgrades to `map<string, ValueType>` so the type of each variable is also tracked:

```cpp
// Chapter 16
static vector<std::map<string, ValueType>> VarScopes;
```

`LookupVarType` searches the scope stack innermost-first, then falls back to `GlobalVarTypes`:

```cpp
static ValueType LookupVarType(const string &Name) {
  for (auto It = VarScopes.rbegin(); It != VarScopes.rend(); ++It) {
    auto Found = It->find(Name);
    if (Found != It->end())
      return Found->second;
  }
  auto GI = GlobalVarTypes.find(Name);
  if (GI != GlobalVarTypes.end())
    return GI->second;
  return ValueType::Error;
}
```

Every `VariableExprAST` carries its resolved type at parse time. When codegen runs, a load uses `LLVMTypeFor(resolvedType)` for the type operand:

```llvm
; var x: int32 — load uses i32
%x_val = load i32, ptr %x

; var r: float32 — load uses float
%r_val = load float, ptr %r
```

## var Declarations: Required Type Annotation

Before:

```python
var x = 1.0
var y
```

After:

```python
var x: float64 = 1.0
var y: int32
var a: int8, b: int16   # multiple bindings in one statement
```

The colon-type is mandatory. `ParseVarStmt` reads it:

```cpp
if (CurTok != ':')
  return LogError(
      "Variable declaration requires a type annotation (e.g., ': int32')");
getNextToken(); // eat ':'
ValueType DeclTy = ParseTypeToken();
if (DeclTy == ValueType::None)
  return LogError("Variables cannot have None type");
```

If there is no initialiser, a zero constant of the declared type is generated. If there is one, the parser installs a `ExpectedLiteralTypeGuard(DeclTy)` before parsing the initializer expression, then checks assignability:

```cpp
{
  ExpectedLiteralTypeGuard Guard(DeclTy);
  Init = ParseExpression();
}
if (!IsAssignable(DeclTy, Init->getType()))
  return LogError("Type mismatch in variable initialization");
```

`VarBinding` replaces the old `pair<string, unique_ptr<ExprAST>>`:

```cpp
struct VarBinding {
  string Name;
  ValueType Ty;
  unique_ptr<ExprAST> Init;
};
```

The generated IR per declaration:

```llvm
; var x: float64 = 1.0
%x = alloca double
store double 1.000000e+00, ptr %x

; var y: int32     (no initializer — zero)
%y = alloca i32
store i32 0, ptr %y

; var a: int8, b: int16
%a = alloca i8
store i8 0, ptr %a
%b = alloca i16
store i16 0, ptr %b

; var ratio: float64 = 3.14
%ratio = alloca double
store double 3.140000e+00, ptr %ratio
```

## for Loops: Typed Loop Variable

```python
# Chapter 15
for i = 1, i <= n, 1:
    body

# Chapter 16
for i: int = 1, i <= n, 1:
    body
```

The `: type` annotation follows the loop variable name directly. The type is validated:

- Must be numeric (`IsNumericType`).
- The start expression must be assignable to it.
- The step expression must be assignable to it.

`ForExprAST` stores `VarType`, and `ForExprAST::codegen` uses `LLVMTypeFor(VarType)` for the `alloca` and the increment:

```cpp
if (IsFloatType(VarType))
  NextVar = Builder->CreateFAdd(CurVar, StepVal, "nextvar");
else
  NextVar = Builder->CreateAdd(CurVar, StepVal, "nextvar");
```

For an integer loop variable the alloca and step use the declared integer type:

```llvm
; for i: int = 1, i <= 10, 1:
%i = alloca i64
store i64 1, ptr %i

loop:
  %i_cur = load i64, ptr %i
  %cmptmp = icmp sle i64 %i_cur, 10
  br i1 %cmptmp, label %body, label %afterloop

body:
  ; ... body ...
  %nextvar = add i64 %i_cur, 1
  store i64 %nextvar, ptr %i
  br label %loop
```

Compare with chapter 15 where `i` would have been `alloca double` and used `fadd` for the increment.

## Explicit Casts

Any type name used as a function call performs a cast:

```python
int32(3.14)     # float64 → int32 (truncates to 3)
float64(42)     # int → float64
int8(x)         # any integer → 8-bit (truncates if needed)
bool(x)         # any value → 0 or 1
float32(n)      # int → float32
```

`ParseCastExpr` is invoked from `ParsePrimary` when the current token is a type keyword:

```cpp
case tok_int:    case tok_int8:   case tok_int16:  case tok_int32:
case tok_int64:  case tok_float:  case tok_float32: case tok_float64:
case tok_bool:
  return ParseCastExpr();
```

`CastExprAST::codegen` delegates to `EmitCast`, which emits one of these instructions depending on the type pair:

```llvm
; int32(3.14)  — float to signed integer (truncates toward zero)
%cast = fptosi double 3.140000e+00 to i32
; result: i32 3

; float64(42)  — integer to double
%cast = sitofp i64 42 to double
; result: double 4.200000e+01

; int8(x) where x: int32  — narrowing truncation
%cast = trunc i32 %x to i8

; int16(x) where x: int8  — widening sign-extension (explicit cast, same as implicit sext)
%cast = sext i8 %x to i16

; float32(n) where n: int32  — integer to single
%cast = sitofp i32 %n to float

; float64(r) where r: float32  — float extension
%cast = fpext float %r to double

; float32(r) where r: float64  — float truncation
%cast = fptrunc double %r to float

; bool(x) where x: int32  — compare against zero
%cast = icmp ne i32 %x, 0
; result: i1

; bool(f) where f: float64  — compare float against zero
%cast = fcmp one double %f, 0.000000e+00
; result: i1
```

## LLVMTypeFor and ZeroConstant

Two helpers translate `ValueType` to LLVM IR constructs.

`LLVMTypeFor` maps each type to its LLVM `Type*`:

```cpp
static Type *LLVMTypeFor(ValueType Ty) {
  switch (Ty) {
  case ValueType::Int: {
    unsigned bits = TheModule->getDataLayout().getPointerSizeInBits();
    return IntegerType::get(*TheContext, bits);
  }
  case ValueType::Int8:    return Type::getInt8Ty(*TheContext);
  case ValueType::Int16:   return Type::getInt16Ty(*TheContext);
  case ValueType::Int32:   return Type::getInt32Ty(*TheContext);
  case ValueType::Int64:   return Type::getInt64Ty(*TheContext);
  case ValueType::Float:   return Type::getDoubleTy(*TheContext);  // same as Float64
  case ValueType::Float32: return Type::getFloatTy(*TheContext);
  case ValueType::Float64: return Type::getDoubleTy(*TheContext);
  case ValueType::Bool:    return Type::getInt1Ty(*TheContext);
  case ValueType::None:    return Type::getVoidTy(*TheContext);
  default:                 return nullptr;
  }
}
```

`Float` and `Float64` both return `getDoubleTy`. In the IR they are indistinguishable.

`ZeroConstant` produces the IR zero initializer for each type, used for uninitialised `var` declarations and global variable default values:

```cpp
static Constant *ZeroConstant(ValueType Ty) {
  switch (Ty) {
  case ValueType::Int8:    return ConstantInt::get(Type::getInt8Ty(*TheContext), 0);
  case ValueType::Int16:   return ConstantInt::get(Type::getInt16Ty(*TheContext), 0);
  case ValueType::Int32:   return ConstantInt::get(Type::getInt32Ty(*TheContext), 0);
  case ValueType::Int:     return ConstantInt::get(LLVMTypeFor(Ty), 0);
  case ValueType::Int64:   return ConstantInt::get(Type::getInt64Ty(*TheContext), 0);
  case ValueType::Float:   return ConstantFP::get(*TheContext, APFloat(0.0));
  case ValueType::Float32: return ConstantFP::get(Type::getFloatTy(*TheContext), 0.0);
  case ValueType::Float64: return ConstantFP::get(*TheContext, APFloat(0.0));
  case ValueType::Bool:    return ConstantInt::get(Type::getInt1Ty(*TheContext), 0);
  default:                 return nullptr;
  }
}
```

Each `ZeroConstant(Ty)` call produces the IR literal you would write inline:

```llvm
i8 0     i16 0     i32 0     i64 0
float 0.000000e+00    double 0.000000e+00    i1 false
```

## IsAssignable and Implicit Widening

`IsAssignable(Dest, Src)` determines whether a value of type `Src` can appear where `Dest` is expected without an explicit cast:

```cpp
static bool IsAssignable(ValueType Dest, ValueType Src) {
  if (Dest == Src)
    return true;
  // float and float64 are interchangeable
  if ((Dest == ValueType::Float && Src == ValueType::Float64) ||
      (Dest == ValueType::Float64 && Src == ValueType::Float))
    return true;
  if (IsIntType(Dest) && IsIntType(Src) && CanWidenInt(Src, Dest))
    return true;
  if (IsFloatType(Dest) && IsIntType(Src))
    return true;
  return false;
}
```

The fourth rule covers `IsFloatType(Dest)` — any integer can widen to any float type (`Float`, `Float32`, or `Float64`).

`CanWidenInt` determines integer widening legality:

```cpp
static bool CanWidenInt(ValueType From, ValueType To) {
  if (From == To) return true;
  // Fixed-size integers: allowed if rank(From) <= rank(To).
  if (IsFixedIntType(From) && IsFixedIntType(To))
    return FixedIntRank(From) <= FixedIntRank(To);
  // Platform int can widen to int64.
  if (From == ValueType::Int && To == ValueType::Int64)
    return true;
  return false;
}
```

Where `FixedIntRank` assigns `Int8=1, Int16=2, Int32=3, Int64=4`.

`IsFixedIntType` covers `Int8`, `Int16`, `Int32`, `Int64` — but not `Int`. `Int` is the platform default integer and is treated separately: it can widen to `Int64`, but not to `Int32` or smaller (since on a 64-bit host `Int` is already 64-bit wide).

Each allowed implicit conversion and the IR it produces:

| Assignment | Allowed? | IR emitted |
|-----------|----------|------------|
| `var x: int16 = int8_val` | Yes | `sext i8 %v to i16` |
| `var x: int32 = int16_val` | Yes | `sext i16 %v to i32` |
| `var x: int64 = int32_val` | Yes | `sext i32 %v to i64` |
| `var x: float64 = int_val` | Yes | `sitofp i64 %v to double` |
| `var x: float32 = int_val` | Yes | `sitofp i64 %v to float` |
| `var x: float = float64_val` | Yes | *(no instruction — same IR type)* |
| `var x: int32 = int64_val` | No | type error |
| `var x: bool = int_val` | No | type error |

`EmitImplicitCast` is called by codegen whenever one of the allowed cases applies:

```cpp
static Value *EmitImplicitCast(Value *V, ValueType From, ValueType To) {
  if (From == To) return V;
  if ((From == ValueType::Float && To == ValueType::Float64) ||
      (From == ValueType::Float64 && To == ValueType::Float))
    return V;  // same IR type — no instruction
  if (IsIntType(From) && IsIntType(To) && CanWidenInt(From, To)) {
    unsigned FromBits = LLVMTypeFor(From)->getIntegerBitWidth();
    unsigned ToBits   = LLVMTypeFor(To)->getIntegerBitWidth();
    if (FromBits == ToBits) return V;
    return Builder->CreateSExt(V, LLVMTypeFor(To), "sext");
  }
  if (IsIntType(From) && IsFloatType(To))
    return Builder->CreateSIToFP(V, LLVMTypeFor(To), "sitofp");
  return nullptr;
}
```

## Binary Operators: Type-Aware Arithmetic

`GetBinaryResultType` decides the type of a binary expression at parse time:

```cpp
static ValueType GetBinaryResultType(int Op, ValueType L, ValueType R) {
  if (IsArithmeticOp(Op)) {
    if (!IsNumericType(L) || !IsNumericType(R))
      return ValueType::Error;
    // Float and Float64 can be mixed — result is Float64
    if (IsFloatType(L) && IsFloatType(R)) {
      if (L == R) return L;
      if ((L == ValueType::Float && R == ValueType::Float64) ||
          (L == ValueType::Float64 && R == ValueType::Float))
        return ValueType::Float64;
      return ValueType::Error;
    }
    if (IsAssignable(L, R)) return L;  // R widens into L
    if (IsAssignable(R, L)) return R;  // L widens into R
    return ValueType::Error;
  }
  if (IsComparisonOp(Op)) {
    if (L == ValueType::Bool && R == ValueType::Bool) {
      if (Op == tok_eq || Op == tok_neq) return ValueType::Bool;
      return ValueType::Error;
    }
    if (!IsNumericType(L) || !IsNumericType(R))
      return ValueType::Error;
    if (IsFloatType(L) && IsFloatType(R)) {
      if (L == R || (L == ValueType::Float && R == ValueType::Float64) ||
          (L == ValueType::Float64 && R == ValueType::Float))
        return ValueType::Bool;
      return ValueType::Error;
    }
    if (IsAssignable(L, R) || IsAssignable(R, L))
      return ValueType::Bool;
    return ValueType::Error;
  }
  // User-defined operators: float64 only
  if (L == ValueType::Float64 && R == ValueType::Float64)
    return ValueType::Float64;
  return ValueType::Error;
}
```

`BinaryExprAST::codegen` implicitly casts both operands to the result type then selects float vs integer instructions:

```cpp
L = EmitImplicitCast(L, LTy, getType());
R = EmitImplicitCast(R, RTy, getType());
if (IsFloatType(getType())) {
  if (Op == '+') return Builder->CreateFAdd(L, R, "addtmp");
  if (Op == '-') return Builder->CreateFSub(L, R, "subtmp");
  return Builder->CreateFMul(L, R, "multmp");
}
if (Op == '+') return Builder->CreateAdd(L, R, "addtmp");
if (Op == '-') return Builder->CreateSub(L, R, "subtmp");
return Builder->CreateMul(L, R, "multmp");
```

Each case and its IR:

```llvm
; int8 + int16 → int16   (int8 widens into int16)
%sext = sext i8 %a to i16
%addtmp = add i16 %sext, %b

; int32 + int64 → int64   (int32 widens into int64)
%sext = sext i32 %a to i64
%addtmp = add i64 %sext, %b

; int32 + float64 → float64   (int32 widens into float64)
%sitofp = sitofp i32 %a to double
%addtmp = fadd double %sitofp, %b

; int32 + float32 → float32   (int32 widens into float32)
%sitofp = sitofp i32 %a to float
%addtmp = fadd float %sitofp, %b

; float + float64 → float64   (Float↔Float64: no cast instruction)
%addtmp = fadd double %a, %b

; int32 < int64 → bool   (int32 widens, result is i1)
%sext = sext i32 %a to i64
%cmptmp = icmp slt i64 %sext, %b

; float64 == float64 → bool
%cmptmp = fcmp oeq double %a, %b

; float32 + float64 → error   (different float sizes; rejected at parse time)
```

Comparisons return `i1` directly — there is no `UIToFP` widening to `double` as there was in chapter 15. The old pattern emerged from having only `double`; now each comparison produces a proper `Bool`.

## EmitCast: Explicit Conversion Table

`EmitCast` handles all explicit `type(expr)` conversions. The full set of instruction choices:

| From | To | IR instruction |
|------|----|----------------|
| any int | float32 | `sitofp iN %v to float` |
| any int | float / float64 | `sitofp iN %v to double` |
| float32 | any int | `fptosi float %v to iN` |
| float / float64 | any int | `fptosi double %v to iN` |
| smaller int | larger int | `sext iN %v to iM` |
| larger int | smaller int | `trunc iN %v to iM` |
| float32 | float / float64 | `fpext float %v to double` |
| float / float64 | float32 | `fptrunc double %v to float` |
| float ↔ float64 | either | *(no instruction — same IR type)* |
| any int | bool | `icmp ne iN %v, 0` |
| any float | bool | `fcmp one double/float %v, 0.0` |

## Void (None) Functions

A `def` without `->` produces a void function:

```python
def greet():        # return type = None (void)
    printd(42.0)
```

Explicit `-> None` is identical at the IR level — it is documentary only:

```python
def greet() -> None:
    printd(42.0)
```

Both produce:

```llvm
define void @greet() {
entry:
  %calltmp = call double @printd(double 4.200000e+01)
  ret void
}
```

The `call` result is present in the IR (LLVM always names it) but no `ret` of that value follows — `ret void` terminates the block.

### ReturnTypeGuard

To validate `return` statements during parsing, a global `CurrentFunctionReturnType` tracks the enclosing function's return type. `ReturnTypeGuard` manages it with RAII:

```cpp
struct ReturnTypeGuard {
  ValueType Saved;
  ReturnTypeGuard(ValueType Ty) : Saved(CurrentFunctionReturnType) {
    CurrentFunctionReturnType = Ty;
  }
  ~ReturnTypeGuard() { CurrentFunctionReturnType = Saved; }
};
```

`ParseDefinition` instantiates a `ReturnTypeGuard` before parsing the body. `ParseReturn` also installs an `ExpectedLiteralTypeGuard(CurrentFunctionReturnType)` before parsing the return value, so bare integer or float literals in `return` statements are given the function's return type directly. Then it validates:

```cpp
// bare 'return' (no value)
if (CurTok == tok_eol || CurTok == tok_dedent || CurTok == tok_eof) {
  if (CurrentFunctionReturnType != ValueType::None)
    return LogError("Return value required");
  return make_unique<ReturnExprAST>(nullptr);
}
// return with value
if (CurrentFunctionReturnType == ValueType::None)
  return LogError("cannot return a value from a None function");
if (!IsAssignable(CurrentFunctionReturnType, Expr->getType()))
  return LogError("cannot return X from function returning Y");
```

The IR for a typed return with an implicit widening:

```llvm
; def sum(a: int8, b: int8) -> int32:
;     return a + b
;
; a + b → int8 (result type of int8+int8)
; int8 is assignable to int32 (widening), so EmitImplicitCast runs

%addtmp = add i8 %a, %b
%sext = sext i8 %addtmp to i32
ret i32 %sext
```

### Implicit Fallthrough

At the end of a function body, if no terminator was emitted:

```cpp
if (!Builder->GetInsertBlock()->getTerminator()) {
  if (P.getReturnType() == ValueType::None) {
    Builder->CreateRetVoid();
  } else {
    if (!IsEntry && pred_empty(CurBB)) {
      Builder->CreateUnreachable();
    } else {
      LogErrorV("Non-None function must return a value");
      TheFunction->eraseFromParent();
      return nullptr;
    }
  }
}
```

For a void function that falls off the end:

```llvm
define void @setup() {
entry:
  ; ... body ...
  ret void          ; inserted automatically
}
```

For a dead block after an early return (e.g., an `if`/`else` where one branch returns):

```llvm
unreachable         ; inserted for pred-empty dead blocks
```

### Void Top-Level Expressions

In the REPL and file-run mode, every top-level expression is wrapped in an anonymous function. If the expression is void (e.g., a call to a void function), the anonymous wrapper must still be correctly typed:

```cpp
ValueType RetTy = Stmt->getType();
if (!Stmt->isReturnExpr() && RetTy != ValueType::None)
  Stmt = make_unique<ReturnExprAST>(std::move(Stmt));

auto Proto = make_unique<PrototypeAST>(
    FnName, vector<pair<string, ValueType>>(), CurLoc, RetTy);
```

```llvm
; calling greet() at top level — wrapper is void
define void @__pyxc.toplevel.3() {
entry:
  call void @greet()
  ret void
}

; evaluating 1 + 2 at top level — wrapper returns int64
define i64 @__pyxc.toplevel.4() {
entry:
  ret i64 3
}
```

`CallExprAST` overrides `shouldPrintValue()` — void calls are silently discarded in the REPL without printing anything.

## The main() Wrapper

When `--emit exe` is used, the user's `main()` must have a C-ABI `int main()` entry point. The wrapper is created unconditionally — regardless of what `main()` returns — and correctly forwards an `int32` return value or substitutes `0` for void:

```cpp
if (auto *UserMain = TheModule->getFunction("main")) {
  UserMain->setName("__pyxc.user_main");
  FunctionType *FT = FunctionType::get(Type::getInt32Ty(*TheContext), false);
  Function *Wrapper = Function::Create(FT, Function::ExternalLinkage,
                                        "main", TheModule.get());
  BasicBlock *BB = BasicBlock::Create(*TheContext, "entry", Wrapper);
  IRBuilder<> TmpB(BB);
  if (UserMain->getReturnType()->isIntegerTy(32)) {
    Value *Ret = TmpB.CreateCall(UserMain);
    TmpB.CreateRet(Ret);
  } else {
    TmpB.CreateCall(UserMain);
    TmpB.CreateRet(ConstantInt::get(Type::getInt32Ty(*TheContext), 0));
  }
}
```

For a `def main() -> int32` user function:

```llvm
define i32 @__pyxc.user_main() {
  ; ... user code ...
  ret i32 0
}

define i32 @main() {
entry:
  %ret = call i32 @__pyxc.user_main()
  ret i32 %ret
}
```

For a `def main()` (void) user function:

```llvm
define void @__pyxc.user_main() {
  ; ... user code ...
  ret void
}

define i32 @main() {
entry:
  call void @__pyxc.user_main()
  ret i32 0
}
```

The wrapper is what the OS and C runtime call. The user-visible `main` function name is preserved by renaming the user's function to `__pyxc.user_main`, then wrapping it.

## JIT Dispatch: Type-Switched Invocation

Before chapter 16, the JIT always called the anonymous top-level function as `double (*)()`. Now the return type determines both the function pointer type and the print format:

```cpp
if (RetTy == ValueType::None) {
  void (*FP)() = ExprSymbol.toPtr<void (*)()>();
  FP();
  // nothing printed
} else {
  switch (RetTy) {
  case ValueType::Float64: {
    double (*FP)() = ExprSymbol.toPtr<double (*)()>();
    double result = FP();
    if (IsRepl && LastTopLevelShouldPrint)
      fprintf(stderr, "%f\n", result);
    break;
  }
  case ValueType::Float32: {
    float (*FP)() = ExprSymbol.toPtr<float (*)()>();
    double result = static_cast<double>(FP());
    if (IsRepl && LastTopLevelShouldPrint)
      fprintf(stderr, "%f\n", result);
    break;
  }
  case ValueType::Int: {
    intptr_t (*FP)() = ExprSymbol.toPtr<intptr_t (*)()>();
    long long result = static_cast<long long>(FP());
    if (IsRepl && LastTopLevelShouldPrint)
      fprintf(stderr, "%lld\n", result);
    break;
  }
  // Int8, Int16, Int32, Int64 all use int*_t pointers and %lld
  case ValueType::Bool: {
    bool (*FP)() = ExprSymbol.toPtr<bool (*)()>();
    bool result = FP();
    if (IsRepl && LastTopLevelShouldPrint)
      fprintf(stderr, "%s\n", result ? "True" : "False");
    break;
  }
  default: break;
  }
}
```

Key points:
- `Float` and `Float64` both print as `%f`.
- Integer types print as `%lld` — decimal integer notation, not floating-point.
- `Bool` prints as `True` or `False` — matching the keyword spelling.
- `None` (void) produces no output.

## Debug Info: Per-Type DWARF Descriptors

Chapter 15 had one `DblDIType` for everything. Chapter 16 needs a descriptor per type:

```cpp
static DIType *IntDIType     = nullptr;
static DIType *Float64DIType = nullptr;
static DIType *Int8DIType    = nullptr;
static DIType *Int16DIType   = nullptr;
static DIType *Int32DIType   = nullptr;
static DIType *Int64DIType   = nullptr;
static DIType *Float32DIType = nullptr;
static DIType *BoolDIType    = nullptr;
```

Initialized in `InitializeDebugInfo`:

```cpp
unsigned bits = TheModule->getDataLayout().getPointerSizeInBits();
IntDIType     = DIB->createBasicType("int",     bits, dwarf::DW_ATE_signed);
Float64DIType = DIB->createBasicType("float64",   64, dwarf::DW_ATE_float);
VoidDIType    = DIB->createUnspecifiedType("None");
Int8DIType    = DIB->createBasicType("int8",       8, dwarf::DW_ATE_signed);
Int16DIType   = DIB->createBasicType("int16",     16, dwarf::DW_ATE_signed);
Int32DIType   = DIB->createBasicType("int32",     32, dwarf::DW_ATE_signed);
Int64DIType   = DIB->createBasicType("int64",     64, dwarf::DW_ATE_signed);
Float32DIType = DIB->createBasicType("float32",   32, dwarf::DW_ATE_float);
BoolDIType    = DIB->createBasicType("bool",       1, dwarf::DW_ATE_boolean);
```

Both `ValueType::Float` and `ValueType::Float64` return `Float64DIType`. In the DWARF output, a debugger sees `int32`, `float64`, `bool`, etc. as distinct named types rather than everything as `double`.

The void type uses `createUnspecifiedType("None")` — the correct DWARF tag `DW_TAG_unspecified_type` for a type with no representation.

## HadError and Exit Codes

Chapter 15 always returned `0`. File-mode programs with type errors would print to stderr but exit cleanly, making shell scripts and test harnesses oblivious to failures.

Chapter 16 adds a global `HadError` flag set by every `LogError` call. File-mode loops check it after parsing:

```cpp
if (HadError) {
  CloseInputFile();
  return 1;
}
```

The final return:

```cpp
if (IsRepl)
  return 0;        // REPL: errors are per-expression and non-fatal
return HadError ? 1 : 0;
```

The REPL keeps running after a type error; file mode aborts with exit code 1.

## Putting It Together: A Full Typed Program

Here is a small program that exercises the type system, and the IR it produces:

```python
extern def printd(x: float64) -> float64

def add(a: int32, b: int32) -> int32:
    return a + b

def main() -> int32:
    var x: int32 = add(10, 5)
    var y: float64 = float64(x)
    printd(y)
    return 0
```

```llvm
declare double @printd(double)

define i32 @add(i32 %a, i32 %b) {
entry:
  %addtmp = add i32 %a, %b
  ret i32 %addtmp
}

define i32 @__pyxc.user_main() {
entry:
  %x = alloca i32
  %call = call i32 @add(i32 10, i32 5)
  store i32 %call, ptr %x

  %y = alloca double
  %x_val = load i32, ptr %x
  %cast = sitofp i32 %x_val to double
  store double %cast, ptr %y

  %y_val = load double, ptr %y
  call double @printd(double %y_val)

  ret i32 0
}

define i32 @main() {
entry:
  %ret = call i32 @__pyxc.user_main()
  ret i32 %ret
}
```

Before chapter 16 every value in this program would have been `double`. Now `add` uses `i32`, the local variable `x` is an `i32` alloca, the `sitofp` appears exactly once and only where the program explicitly asked for it with `float64(x)`, and `main` has a proper `i32` return type.

## Known Limitations

**No operator overloading.** Each operator character maps to exactly one function (`binary+`, `unary!`). Redefining an operator that is already defined is rejected at parse time. You cannot have two versions of the same operator that differ only in their parameter types.

**`None` cannot be used as a variable type.** `var x: None` is rejected. `None` is only valid as a return type annotation.

**`Int` does not widen to fixed-size integers.** `Int` (pointer-width) can widen to `Int64`, but not to `Int32` or smaller — even on a 32-bit host where they would have the same width. Use an explicit cast when crossing `Int`/`Int32` boundaries.

**`float32 + float64` is a type error.** The two float sizes are not interchangeable in binary operations — only `float` and `float64` are. Use an explicit cast: `float64(x) + y`.

## Try It

**Boolean literals**

```python
ready> True
True
ready> False
False
ready> True == False
False
```

**REPL prints by type**

```python
ready> var n: int32 = 42
ready> n
42
ready> var x: float64 = 3.14
ready> x
3.140000
ready> True
True
```

**Trigger a type error**

```python
# mismatch.pyxc
def add(a: int32, b: int32) -> int32:
    return a + b
add(1.0, 2.0)  # Error: argument 1 expects int32
```

```bash
pyxc mismatch.pyxc  # exits with status 1
```

**Mixed int sizes — widening is automatic**

```python
var a: int8 = 10
var b: int16 = 200
var c: int32 = a + b   # int8 widens to int16, result int16 widens to int32
```

**Explicit cast round-trip**

```python
var x: float64 = 3.99
var y: int32 = int32(x)      # fptosi → 3
var z: float64 = float64(y)  # sitofp → 3.0
```

**Inspect the IR**

```bash
pyxc --emit llvm-ir -o out.ll program.pyxc
grep 'define\|alloca\|fptosi\|sitofp\|sext\|fadd\|add ' out.ll
```

## Build and Run

```bash
cd code/chapter-16
cmake -S . -B build && cmake --build build
echo "var x: int32 = 7" | ./build/pyxc
```

## What's Next

Pyxc now has a real type system with ten scalar types, explicit casts, typed parameters and return values, and a void type for side-effecting functions. The next step is aggregate types — structs — which require extending the type system beyond scalars and introducing memory layout decisions. The debug info infrastructure built in chapters 15 and 16 will handle them immediately once the right DWARF descriptors are wired in.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)
