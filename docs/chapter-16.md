---
description: "Add a static type system: int, int8, int16, int32, int64, float32, float64, bool, and void (None). Parameters, variables, and return types are all explicitly annotated."
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
```

Key changes from chapter 15:

- Every parameter requires `: type`.
- `var` declarations require `: type`, optionally followed by `= expression`.
- `for` loop variables require `: type` between the name and `=`.
- A function carries `-> type` between `)` and `:`. If absent, `def` defaults to `None` (void); `extern def` and operator defs default to `float64`.
- `None` is the void return type annotation; it cannot be used as a variable or parameter type.

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
  Float32, // 32-bit float
  Float64, // 64-bit double
  Bool,    // 1-bit boolean (i1)
  Error    // sentinel — parse/type error
};
```

`Int` (no size suffix) maps to the pointer-width integer on the target machine. On a 64-bit host it is 64 bits; on a 32-bit host it is 32 bits. It corresponds to C's `int` on most platforms when written without a size.

`Int32` is always 32 bits regardless of target. `int32(x)` is a reliable cross-platform 32-bit integer; `int` is a platform-default convenience.

`Float64` is also exposed as `float` — both keywords map to the same token and produce identical IR. `float` exists purely as a shorter alias.

`None` is the return type of functions that produce no value. It corresponds to LLVM's `void` and cannot be used as a variable or parameter type.

`Error` is a sentinel returned by type helpers when something is wrong. It is never a valid type for any value; it propagates errors without needing `Optional`.

### Implicit Conversions

The type system is strict. The only implicit conversions allowed are:

1. **Same type → always allowed.**
2. **Integer widening** — smaller fixed-size integers can be assigned to larger ones: `Int8 → Int16 → Int32 → Int64`. Also, `Int` (pointer-width) can widen to `Int64`.
3. **Any integer type → `Float64`** — an integer can be silently widened to a double when the context requires it.

Everything else requires an explicit cast. In particular:
- You cannot mix `int32` and `float32` implicitly — `float32` does not participate in integer widening.
- Narrowing (e.g., `int64` to `int32`) always requires an explicit cast.
- `Bool` is not implicitly assignable from any other type.

## New Tokens

Two sets of tokens are added.

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
tok_float32 = -27, tok_float64 = -28,
tok_bool = -29,
tok_none = -30,
```

Registered in the keyword map:

```cpp
{"int", tok_int},         {"int8", tok_int8},     {"int16", tok_int16},
{"int32", tok_int32},     {"int64", tok_int64},
{"float32", tok_float32}, {"float64", tok_float64},
{"float", tok_float64},   // alias — same token as float64
{"bool", tok_bool},
{"None", tok_none}
```

`float` maps to the same token as `float64`, so there is no distinction in the AST or IR.

## Numeric Literal Types

Before chapter 16, every number literal was a `double`. Now the lexer sets a flag when scanning:

```cpp
NumIsFloat = NumStr.find('.') != string::npos;
```

`ParseNumber` uses this to choose the literal's type:

```cpp
ValueType Ty = NumIsFloat ? ValueType::Float64 : ValueType::Int;
auto Result = make_unique<NumberExprAST>(NumVal, Ty);
```

`42` is an `Int` literal and `42.0` is a `Float64` literal. Both store the same `double` internally in the AST node; `NumberExprAST::codegen` distinguishes at IR time:

```cpp
if (getType() == ValueType::Int)
  return ConstantInt::get(LLVMTypeFor(getType()), static_cast<int64_t>(Val));
return ConstantFP::get(*TheContext, APFloat(Val));
```

## ParseTypeToken and ParseOptionalReturnType

`ParseTypeToken` consumes the current token if it is a type keyword and returns the corresponding `ValueType`:

```cpp
static ValueType ParseTypeToken() {
  switch (CurTok) {
  case tok_int:    getNextToken(); return ValueType::Int;
  case tok_int32:  getNextToken(); return ValueType::Int32;
  case tok_float64: getNextToken(); return ValueType::Float64;
  case tok_none:   getNextToken(); return ValueType::None;
  case tok_int8:   getNextToken(); return ValueType::Int8;
  case tok_int16:  getNextToken(); return ValueType::Int16;
  case tok_int64:  getNextToken(); return ValueType::Int64;
  case tok_float32: getNextToken(); return ValueType::Float32;
  case tok_bool:   getNextToken(); return ValueType::Bool;
  default:
    LogError("Expected a type (int, int8, int16, int32, int64, "
             "float, float32, float64, bool, or None)");
    return ValueType::Error;
  }
}
```

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
- `ParseExtern` and operator parsers call `ParseOptionalReturnType()` (default `Float64`) — extern declarations and operator overloads default to `float64` because they are typically C library functions or mathematical operators.

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

Every `VariableExprAST` carries its resolved type at parse time. If the name is unknown and the parser is at top-level (REPL), it defaults to `Float64` for backward compatibility with bare-name expressions in the REPL.

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

If there is no initialiser, a zero constant of the declared type is generated. If there is one, the parser checks assignability:

```cpp
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
if (VarType == ValueType::Float64)
  NextVar = Builder->CreateFAdd(CurVar, StepVal, "nextvar");
else
  NextVar = Builder->CreateAdd(CurVar, StepVal, "nextvar");
```

## Explicit Casts

Any type name used as a function call performs a cast:

```python
int32(3.14)     # float64 → int32 (truncates to 3)
float64(42)     # int → float64
int8(x)         # any integer → 8-bit (truncates if needed)
bool(x)         # any value → 0 or 1
int16(n)        # widening or narrowing, always explicit
```

`ParseCastExpr` is invoked from `ParsePrimary` when the current token is a type keyword:

```cpp
case tok_int:    case tok_int8:
case tok_int16:  case tok_int32:
case tok_int64:  case tok_float32:
case tok_float64: case tok_bool:
  return ParseCastExpr();
```

`ParseCastExpr` itself:

```cpp
static unique_ptr<ExprAST> ParseCastExpr() {
  ValueType Ty = ParseTypeToken();
  if (Ty == ValueType::None)
    return LogError("Cannot cast to None");
  if (CurTok != '(')
    return LogError("Expected '(' after cast type");
  getNextToken(); // eat '('
  auto Expr = ParseExpression();
  if (CurTok != ')')
    return LogError("Expected ')' after cast expression");
  getNextToken(); // eat ')'
  return make_unique<CastExprAST>(Ty, std::move(Expr));
}
```

`CastExprAST::codegen` delegates to `EmitCast`:

```cpp
Value *CastExprAST::codegen() {
  Value *V = Expr->codegen();
  Value *Cast = EmitCast(V, Expr->getType(), TargetTy);
  if (!Cast) return LogErrorV("Invalid cast");
  return Cast;
}
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
  case ValueType::Float32: return Type::getFloatTy(*TheContext);
  case ValueType::Float64: return Type::getDoubleTy(*TheContext);
  case ValueType::Bool:    return Type::getInt1Ty(*TheContext);
  case ValueType::None:    return Type::getVoidTy(*TheContext);
  default:                 return nullptr;
  }
}
```

`Int32` always produces `i32`. `Int` queries the data layout for pointer width, so on a 64-bit host `LLVMTypeFor(Int) == i64`, on 32-bit it is `i32`.

`ZeroConstant` produces the zero initializer for each type, used when a `var` has no explicit initializer and when global variables are populated:

```cpp
static Constant *ZeroConstant(ValueType Ty) {
  switch (Ty) {
  case ValueType::Int8:    return ConstantInt::get(Type::getInt8Ty(*TheContext), 0);
  case ValueType::Int16:   return ConstantInt::get(Type::getInt16Ty(*TheContext), 0);
  case ValueType::Int32:   return ConstantInt::get(Type::getInt32Ty(*TheContext), 0);
  case ValueType::Int:     return ConstantInt::get(LLVMTypeFor(Ty), 0);
  case ValueType::Int64:   return ConstantInt::get(Type::getInt64Ty(*TheContext), 0);
  case ValueType::Float32: return ConstantFP::get(Type::getFloatTy(*TheContext), 0.0);
  case ValueType::Float64: return ConstantFP::get(*TheContext, APFloat(0.0));
  case ValueType::Bool:    return ConstantInt::get(Type::getInt1Ty(*TheContext), 0);
  default:                 return nullptr;
  }
}
```

## IsAssignable and Implicit Widening

`IsAssignable(Dest, Src)` determines whether a value of type `Src` can appear where `Dest` is expected without an explicit cast:

```cpp
static bool IsAssignable(ValueType Dest, ValueType Src) {
  if (Dest == Src)
    return true;
  if (IsIntType(Dest) && IsIntType(Src) && CanWidenInt(Src, Dest))
    return true;
  if (Dest == ValueType::Float64 && IsIntType(Src))
    return true;
  return false;
}
```

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

`IsFixedIntType` covers `Int8`, `Int16`, `Int32`, `Int64` — but not `Int`. `Int` is the platform default integer and is treated separately: it can widen to `Int64`, but not to `Int32` (since on a 64-bit host `Int` is already 64-bit wide and forcing it into 32 bits would silently truncate).

The practical consequence:

| Expression | Allowed? | Reason |
|-----------|----------|--------|
| `var x: int16 = int8_val` | Yes | rank widening |
| `var x: int32 = int16_val` | Yes | rank widening |
| `var x: int64 = int32_val` | Yes | rank widening |
| `var x: float64 = int_val` | Yes | int → float64 |
| `var x: float32 = int_val` | No | float32 not in widening chain |
| `var x: int32 = int64_val` | No | narrowing |
| `var x: bool = int_val` | No | no bool coercion |

## Binary Operators: Type-Aware Arithmetic

`GetBinaryResultType` decides the type of a binary expression at parse time using `IsAssignable` symmetrically:

```cpp
static ValueType GetBinaryResultType(int Op, ValueType L, ValueType R) {
  if (IsArithmeticOp(Op)) {
    if (!IsNumericType(L) || !IsNumericType(R))
      return ValueType::Error;
    if (IsAssignable(L, R)) return L;  // R widens into L
    if (IsAssignable(R, L)) return R;  // L widens into R
    return ValueType::Error;
  }
  if (IsComparisonOp(Op)) {
    // same logic
  }
  // User-defined operators: float64 only
  if (L == ValueType::Float64 && R == ValueType::Float64)
    return ValueType::Float64;
  return ValueType::Error;
}
```

This means:

- `int8 + int16` → result type `int16` (int8 widens into int16)
- `int32 + float64` → result type `float64` (int32 widens into float64)
- `int32 + float32` → error (neither is assignable to the other)
- `int32 + int64` → result type `int64` (int32 widens into int64)

`BinaryExprAST::codegen` then implicitly casts both operands to the result type before emitting the instruction, and selects float vs integer instructions accordingly:

```cpp
// arithmetic path
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

Comparisons follow the same pattern — `FCmp*` for floats, `ICmpS*` for integers. The old pattern of widening comparison results to `double` via `UIToFP` is replaced: integer comparisons extend with `ZExt` to the result type.

## EmitCast and EmitImplicitCast

`EmitCast` handles all explicit conversion combinations:

| From | To | Instruction |
|------|----|-------------|
| any int | any float | `SIToFP` |
| any float | any int | `FPToSI` |
| smaller int | larger int | `SExt` (sign-extend) |
| larger int | smaller int | `Trunc` |
| float32 | float64 | `FPExt` |
| float64 | float32 | `FPTrunc` |
| any numeric | bool | `ICmpNE`/`FCmpONE` against zero |

`EmitImplicitCast` handles only the subset of implicit conversions (what `IsAssignable` allows) and returns `nullptr` for anything else, propagating errors:

```cpp
static Value *EmitImplicitCast(Value *V, ValueType From, ValueType To) {
  if (From == To) return V;
  if (IsIntType(From) && IsIntType(To) && CanWidenInt(From, To)) {
    unsigned FromBits = LLVMTypeFor(From)->getIntegerBitWidth();
    unsigned ToBits   = LLVMTypeFor(To)->getIntegerBitWidth();
    if (FromBits == ToBits) return V;
    return Builder->CreateSExt(V, LLVMTypeFor(To), "sext");
  }
  if (IsIntType(From) && To == ValueType::Float64)
    return Builder->CreateSIToFP(V, LLVMTypeFor(To), "sitofp");
  return nullptr;
}
```

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

`ParseDefinition` instantiates a `ReturnTypeGuard` before parsing the body. `ParseReturn` consults it:

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

### Codegen for Bare Return

`ReturnExprAST::codegen` handles both cases:

```cpp
if (!Expr) {
  Builder->CreateRetVoid();
  return ConstantFP::get(*TheContext, APFloat(0.0)); // dummy — never used
}
// implicit cast then ret
RetVal = EmitImplicitCast(RetVal, Expr->getType(), CurrentFunctionReturnType);
Builder->CreateRet(RetVal);
```

### Implicit Fallthrough

At the end of a function body, if no terminator was emitted:

```cpp
if (!Builder->GetInsertBlock()->getTerminator()) {
  if (P.getReturnType() == ValueType::None) {
    Builder->CreateRetVoid();
  } else {
    // dead block (e.g., after an early return) — mark unreachable
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

Void functions fall through to `ret void`. Non-void functions that reach the end of their body without a `return` are an error — the function is erased.

### Void Top-Level Expressions

In the REPL and file-run mode, every top-level expression is wrapped in an anonymous function. If the expression is void (e.g., a call to a void function), the anonymous wrapper must still be correctly typed. The fix is to track the return type at construction time and give the wrapper the correct prototype:

```cpp
ValueType RetTy = Stmt->getType();
if (!Stmt->isReturnExpr() && RetTy != ValueType::None)
  Stmt = make_unique<ReturnExprAST>(std::move(Stmt));

auto Proto = make_unique<PrototypeAST>(
    FnName, vector<pair<string, ValueType>>(), CurLoc, RetTy);
```

`CallExprAST` overrides `shouldPrintValue()`:

```cpp
bool shouldPrintValue() const override {
  return getType() != ValueType::None;
}
```

A void call result is silently discarded in the REPL without printing `0.000000`.

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
    TmpB.CreateRet(Ret);          // forward int32 return value
  } else {
    TmpB.CreateCall(UserMain);
    TmpB.CreateRet(ConstantInt::get(Type::getInt32Ty(*TheContext), 0));
  }
}
```

`main()` is restricted to `None` or `int` return types. Any other return type is rejected with an error before the wrapper is created.

## JIT Dispatch: Type-Switched Invocation

Before chapter 16, the JIT always called the anonymous top-level function as `double (*)()`. Now the return type determines the function pointer type:

```cpp
if (RetTy == ValueType::None) {
  void (*FP)() = ExprSymbol.toPtr<void (*)()>();
  FP();
} else {
  double result = 0.0;
  switch (RetTy) {
  case ValueType::Float64: { double (*FP)()    = ...; result = FP(); break; }
  case ValueType::Float32: { float (*FP)()     = ...; result = FP(); break; }
  case ValueType::Int:     { intptr_t (*FP)()  = ...; result = FP(); break; }
  case ValueType::Int8:    { int8_t (*FP)()    = ...; result = FP(); break; }
  case ValueType::Int16:   { int16_t (*FP)()   = ...; result = FP(); break; }
  case ValueType::Int64:   { int64_t (*FP)()   = ...; result = FP(); break; }
  case ValueType::Bool:    { bool (*FP)()      = ...; result = FP(); break; }
  default: break;
  }
  if (IsRepl && LastTopLevelShouldPrint)
    fprintf(stderr, "%f\n", result);
}
```

All integer and float results are widened to `double` for display. The REPL still prints in `%f` format.

For file-mode JIT, `main()` is similarly dispatched:

```cpp
if (MainIt->second->getReturnType() == ValueType::Int) {
  int (*MainFn)() = MainSymbol.toPtr<int (*)()>();
  MainFn();
} else {
  void (*MainFn)() = MainSymbol.toPtr<void (*)()>();
  MainFn();
}
```

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
Int8DIType    = DIB->createBasicType("int8",      8,  dwarf::DW_ATE_signed);
Int16DIType   = DIB->createBasicType("int16",     16, dwarf::DW_ATE_signed);
Int32DIType   = DIB->createBasicType("int32",     32, dwarf::DW_ATE_signed);
Int64DIType   = DIB->createBasicType("int64",     64, dwarf::DW_ATE_signed);
Float32DIType = DIB->createBasicType("float32",   32, dwarf::DW_ATE_float);
BoolDIType    = DIB->createBasicType("bool",       1, dwarf::DW_ATE_boolean);
```

`DITypeFor(ValueType)` dispatches to the right descriptor. `EmitDebugDeclare` and `EmitDebugGlobal` now take a `ValueType` parameter. In the DWARF output, a debugger sees `int32`, `float64`, `bool`, etc. as distinct named types rather than everything as `double`.

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

## What the IR Looks Like

A typed function:

```llvm
; Chapter 15 — everything is double
define double @add(double %a, double %b) {
entry:
  %addtmp = fadd double %a, %b
  ret double %addtmp
}

; Chapter 16 — typed
define i32 @add(i32 %a, i32 %b) {
entry:
  %addtmp = add i32 %a, %b
  ret i32 %addtmp
}
```

A void function:

```llvm
define void @greet() {
entry:
  %calltmp = call double @printd(double 4.200000e+01)
  ret void
}
```

An integer widening in a binary operation (`int8 + int16`):

```llvm
%sext = sext i8 %x to i16
%addtmp = add i16 %sext, %y
```

A cast expression `int32(3.14)`:

```llvm
%fptosi = fptosi double 3.140000e+00 to i32
```

## Known Limitations

**No operator overloading.** Each operator character maps to exactly one function (`binary+`, `unary!`). Redefining an operator that is already defined is rejected at parse time. You cannot have two versions of the same operator that differ only in their parameter types. Full overloading requires name-mangling and overload resolution — infrastructure that is deferred to a later chapter.

**`None` cannot be used as a variable type.** `var x: None` is rejected. `None` is only valid as a return type annotation.

**`Int` does not widen to fixed-size integers.** `Int` (pointer-width) can widen to `Int64`, but not to `Int32` or smaller — even on a 32-bit host where they would have the same width. Use an explicit cast when crossing `Int`/`Int32` boundaries.

**REPL always displays as `%f`.** All results are widened to `double` before printing. Integer values display as floating-point notation (`42.000000` not `42`).

## Try It

**Inspect typed IR**

```bash
pyxc --emit llvm-ir -o out.ll program.pyxc
grep 'define\|alloca\|fptosi\|sitofp\|sext' out.ll
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
var c: int32 = a + b   # ok — int8 widens to int16, then int16 widens to int32
```

**Explicit cast round-trip**

```python
var x: float64 = 3.99
var y: int32 = int32(x)      # truncates to 3
var z: float64 = float64(y)  # widens back to 3.0
```

**Void function in a for loop**

```python
extern def printd(x: float64) -> float64

def print_range(n: int):
    for i: int = 1, i <= n, 1:
        printd(float64(i))
```

## Build and Run

```bash
cd code/chapter-16
cmake -S . -B build && cmake --build build
echo "var x: int32 = 7" | ./build/pyxc
```

## What's Next

Pyxc now has a real type system with nine scalar types, explicit casts, typed parameters and return values, and a void type for side-effecting functions. The next step is aggregate types — structs — which require extending the type system beyond scalars and introducing memory layout decisions. The debug info infrastructure built in chapters 15 and 16 will handle them immediately once the right DWARF descriptors are wired in.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)
