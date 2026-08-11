---
description: "Add a static type system: int, int8, int16, int32, int64, float, float32, float64, bool, and void (None). Parameters, variables, and return types are all explicitly annotated."
---
# 18. pyxc: A Static Type System

## What I Am Building

[Chapter 17](chapter-17.md) gave me `--emit exe` and one-step native executables. The language itself still has exactly one type: `double`. Every variable, parameter, return value, and literal is a 64-bit float, and I never need to ask "what type is this?".

This chapter adds a real type system. After this chapter:

```pyxc
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
cd pyxc-llvm-tutorial/code/chapter-18
```

## Grammar

The grammar gains type annotations throughout, diffing directly against chapter 17's:

```grammardiff
 program         = [ end-of-lines ] [ top-level-item { end-of-lines top-level-item } ] [ end-of-lines ] ;
 end-of-lines            = end-of-line { end-of-line } ;
 top-level-item             = function-definition | external | top-level-expression ;
-function-definition      = "def" function-signature ":" ( simple-statement | end-of-lines block ) ;
-external        = "extern" "def" function-signature ;
+function-definition      = "def" function-signature [ "->" type ] ":" ( simple-statement | end-of-lines block ) ;
+(* If the return type is omitted, it defaults to None. *)
+external        = "extern" "def" function-signature [ "->" type ] ;
 top-level-expression    = expression ;
-function-signature       = name "(" [ parameters ] ")" ;
-parameters               = parameter { "," parameter } ;
-parameter                = name ;
+function-signature       = name "(" [ typed-parameter { "," typed-parameter } ] ")" ;
+typed-parameter      = name ":" type ;
 if-statement          = "if" expression ":" suite
                 [ end-of-lines "else" ":" suite ] ;
-for-statement         = "for" [ "var" ] name "=" expression "," expression "," expression ":" suite ;
+for-statement         = "for"
+                  ( "var" name ":" type | name )
+                  "=" expression "," expression "," expression ":" suite ;
 variable-statement         = "var" variable-binding { "," variable-binding } ;
 assignment-statement      = lvalue "=" expression ; (* assignment is a statement here *)
 simple-statement      = return-statement | variable-statement | assignment-statement | expression ;
 compound-statement    = if-statement | for-statement ;
 statement       = simple-statement | compound-statement ;
 suite           = simple-statement | compound-statement | end-of-lines block ;
-return-statement      = "return" expression ;
+return-statement      = "return" [ expression ] ;
 statement-separator = end-of-lines | BLOCK_END ;
 block = indent statement { statement-separator statement } dedent ;
 expression      = comparison ;
 comparison               = sum { comparison-operator sum } ;
 comparison-operator      = "==" | "!=" | "<=" | ">=" | "<" | ">" ;
 sum                      = term { ("+" | "-") term } ;
 term                     = unary-expression { ("*" | "/") unary-expression } ;
 lvalue          = name ;
-variable-binding      = name [ "=" expression ] ;
+variable-binding      = name ":" type [ "=" expression ] ;
 unary-expression       = "-" unary-expression | primary ;
-primary         = name-expression | number-expression | parenthesized-expression ;
+primary         = cast-expression | name-expression | number-expression | boolean-literal | parenthesized-expression ;
+cast-expression        = cast-type "(" expression ")" ;
 name-expression  = name | call-expression ;
-call-expression        = name "(" [ arguments ] ")" ;
-arguments              = expression { "," expression } ;
+call-expression        = name "(" [ expression { "," expression } ] ")" ;
 number-expression      = number ;
 parenthesized-expression       = "(" expression ")" ;
 indent          = INDENT ;
 dedent          = DEDENT ;
 
 name      = (letter | "_") { letter | digit | "_" } ;
-number          = digit { digit } [ "." { digit } ]
-                | "." digit { digit } ;
+type            = "int" | "int8" | "int16" | "int32" | "int64"
+                | "float" | "float32" | "float64"
+                | "bool" | "None" ;
+cast-type        = "int" | "int8" | "int16" | "int32" | "int64"
+                | "float" | "float32" | "float64"
+                | "bool" ;
+integer         = digit { digit } ;
+number          = ( digit { digit } [ "." { digit } ]
+                  | "." digit { digit } ) [ exponent ] ;
+exponent        = ( "e" | "E" ) [ "+" | "-" ] digit { digit } ;
+boolean-literal    = "True" | "False" ;
 letter          = "A".."Z" | "a".."z" ;
 digit           = "0".."9" ;
 end-of-line             = "\r\n" | "\r" | "\n" ;
 comment = "#" { comment-character } ;
 comment-character = ? any character except "\r" and "\n" ? ;
 whitespace = " " | "\t" | "\v" | "\f" ;
 INDENT          = ? synthetic token emitted by lexer ? ;
 DEDENT          = ? synthetic token emitted by lexer ? ;
 
 BLOCK_END = ? synthetic token injected into the stream by ParseBlock immediately after it consumes DEDENT ? ;
```

Summary of what changed:

- Every parameter requires `: type` (`typed-parameter` replaces the bare `name` in `function-signature`).
- `var` declarations require `: type` between the name and an optional `= expression`.
- `for var` loop variables require `: type` between the name and `=`.
  A plain `for i = ...` reuses an existing variable and does not accept a type.
- A function carries `[ "->" type ]` between `)` and `:`. If absent, `def` defaults to `None` (void); `extern def` defaults to `float64`.
- `None` is the void return type annotation; it cannot be used as a variable or parameter type.
- `return` no longer requires a value: `return-statement` is now `"return" [ expression ]`, so a bare `return` is legal inside a `None` function.
- `True` and `False` are new boolean literal keywords (`boolean-literal`), and `cast-expression` (`type "(" expression ")"`) is a new kind of `primary`.
- Number literals accept an optional exponent suffix, `e` or `E` followed by an optional sign and digits (`2e3`, `1.5e-2`). An exponent makes the literal a float even with no `.`.

## The Design

### Representing a Value's Type

Every value now has a type, represented as a `ValueType` enum:

```cpp
enum class ValueType {
  None,
  Int, /* depends on system default for int */
  Int8,
  Int16,
  Int32,
  Int64,
  Float,
  Float32,
  Float64,
  Bool,
  Error
};
```

`None` is void, no return value. `Int` is whatever the target machine's default integer width is, 32 or 64 bits depending on host. `Int8` through `Int64` are the fixed-width signed integers. `Float` and `Float64` both mean a 64-bit double (more on why there are two names for the same thing below); `Float32` is a 32-bit float. `Bool` is a 1-bit boolean (`i1` in LLVM). `Error` is the sentinel I return from type helpers when something's wrong.

Each `ValueType` maps to a fixed LLVM IR type:

| ValueType | Keyword | LLVM IR type | Notes |
|-----------|---------|--------------|-------|
| `Int` | `int` | `i64` / `i32` | pointer-width: host-dependent |
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

`Float` and `Float64` are **distinct enum values but compile to the same IR type**: both produce `double` from `LLVMTypeFor`. The `float` keyword gives you `ValueType::Float`; the `float64` keyword gives you `ValueType::Float64`. They are interchangeable everywhere: assignment, binary operations, function arguments, and debug info all treat them as the same 64-bit float. The distinction exists at the token and enum level only so that error messages and `TypeName()` can round-trip the exact keyword the programmer wrote.

`None` is the return type of functions that produce no value. It corresponds to LLVM's `void` and cannot be used as a variable or parameter type.

`Error` is a sentinel returned by type helpers when something is wrong. It propagates errors without needing `Optional`.

### Implicit Conversions

The type system is strict. The only implicit conversions allowed are:

1. **Same type → always allowed.**
2. **Float ↔ Float64**: the two 64-bit float spellings are freely interchangeable. No instruction is emitted since they share the same IR type.
3. **Integer widening**: smaller fixed-size integers can be assigned to larger ones: `Int8 → Int16 → Int32 → Int64`. Also, `Int` (pointer-width) can widen to `Int64`. Emits `sext`.
4. **Any integer type → any float type**: an integer can be silently converted to `Float`, `Float32`, or `Float64`. Emits `sitofp` into whichever float width the destination actually is.
5. **Float32 → Float/Float64**: a narrower float widens to a wider one. Emits `fpext`. The reverse (`Float64 → Float32`) is narrowing and needs an explicit cast, same as integers.

Everything else requires an explicit cast. In particular:
- Narrowing (e.g., `int64` to `int32`, or `float64` to `float32`) always requires an explicit cast.
- `Bool` is not implicitly assignable from any other type.
- `Int` does not widen to `Int32` or smaller fixed-size types (use an explicit cast).

I confirmed rule 4 by compiling `var r: float32 = n` for an `int32 n`: it produces a clean `sitofp i32 %n to float`, no error, exactly like the `float64` case. `EmitImplicitCast`'s int-to-float branch calls `LLVMTypeFor(To)`, which already resolves to whichever float width `To` actually is, so there was never a `Float64`-only restriction to trip over here.

In IR, the four implicit cases look like:

```llvm
; Integer widening: int8 → int16
%wide = sext i8 %x to i16

; Integer → float: int32 → float64
%asf = sitofp i32 %n to double

; Integer → float32: int32 → float32
%asf32 = sitofp i32 %n to float

; Float32 → Float64: widening
%wide32 = fpext float %r to double

; Float ↔ Float64: no instruction: same IR type double
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
  int Tok = (peek() == '>') ? (advance(), tok_arrow) : tok_minus;
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

`float` and `float64` are **separate tokens**: `tok_float = -27` and `tok_float64 = -29`. `ParseTypeToken` maps `tok_float → ValueType::Float` and `tok_float64 → ValueType::Float64`. Both compile to `double`, but the distinction is preserved through the entire pipeline until IR emission.

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

Before chapter 17, every number literal stored a `double`. Now literals have proper types. The lexer sets a flag:

```cpp
NumberIsFloat = NumStr.find('.') != string::npos;
```

`ParseNumberExpression` uses `APInt` or `APFloat` depending on this flag:

```cpp
static unique_ptr<ExpressionNode> ParseNumberExpression() {
  ValueType Type = NumberIsFloat ? ValueType::Float64 : ValueType::Int;
  if (NumberIsFloat) {
    if (IsFloatType(ExpectedLiteralType))
      Type = ExpectedLiteralType;
    const fltSemantics &Semantics = (Type == ValueType::Float32)
                                        ? APFloat::IEEEsingle()
                                        : APFloat::IEEEdouble();
    APFloat Val(Semantics);
    auto StatusOrErr =
        Val.convertFromString(NumberLiteral, APFloat::rmNearestTiesToEven);
    if (!StatusOrErr)
      return LogErrorExpression("Invalid floating-point literal");
    APFloat::opStatus Status = *StatusOrErr;
    if (Status & APFloat::opInvalidOp)
      return LogErrorExpression("Invalid floating-point literal");
    if (Status & APFloat::opOverflow)
      return LogErrorExpression("Floating-point literal out of range for type");
    auto Result = make_unique<NumberExpressionNode>(Val, Type);
    getNextToken(); // consume the number
    return std::move(Result);
  } else {
    // If the surrounding context expects an int type, honor it.
    if (IsIntType(ExpectedLiteralType))
      Type = ExpectedLiteralType;

    // Parse with enough bits to hold the literal's full magnitude, then
    // range-check against the target *signed* width. This avoids cases like
    // 128: it needs 8 bits unsigned, but doesn't fit in signed int8 (max 127).
    unsigned Bits = LLVMTypeFor(Type)->getIntegerBitWidth();
    unsigned NeededBits = APInt::getBitsNeeded(NumberLiteral, 10);
    unsigned ParseBits = std::max(Bits, NeededBits);
    APInt Val(ParseBits, NumberLiteral, 10);
    if (Val.ugt(APInt::getSignedMaxValue(Bits)))
      return LogErrorExpression("Integer literal out of range for type");
    if (ParseBits != Bits)
      Val = Val.trunc(Bits);

    auto Result = make_unique<NumberExpressionNode>(Val, Type);
    getNextToken(); // consume the number
    return std::move(Result);
  }
}
```

The lexer sets `NumberIsFloat` from both the decimal point and the exponent: `NumberIsFloat = SawDot || SawExp;`. A literal like `2e3` has no `.` but still becomes a `Float64` (`2000.0`), since an exponent alone makes a literal float. I confirmed this by compiling `2e3` directly: chapter 17, which has no exponent support at all, rejects it outright with "Unexpected name 'e3'"; chapter 18 accepts it and prints `2000.000000`.

`NumberExpressionNode` stores the literal with full precision:

```cpp
class NumberExpressionNode : public ExpressionNode {
  bool IsIntLiteral;
  APInt IntegerValue;
  APFloat FloatValue;

public:
  NumberExpressionNode(APInt Value, ValueType Type)
      : IsIntLiteral(true), IntegerValue(std::move(Value)), FloatValue(0.0) {
    setType(Type);
  }
  NumberExpressionNode(APFloat Value, ValueType Type)
      : IsIntLiteral(false), IntegerValue(1, 0), FloatValue(std::move(Value)) {
    setType(Type);
  }
  llvm::Value *codegen() override;
};
```

The IR constants each produces:

```llvm
; 42  : integer literal, no '.', defaults to Int (i64 on 64-bit host)
i64 42

; 42.0: float literal, has '.', defaults to Float64
double 4.200000e+01

; var x: int32 = 5  : context sets int32, literal is parsed as i32 directly
i32 5

; var x: float32 = 1.5 : context sets float32, literal parsed at single precision
float 1.500000e+00

; var b: int8 = 200  : out of range for i8, parse error before any IR is emitted
; Error: Integer literal out of range for type
```

`3.14` in a `var x: float32` context becomes a `Float32` literal parsed with IEEE single precision: so `3.14` stores the nearest `float` value, not the nearest `double`. Without this, the parse would produce a `double` constant and then require an `fptrunc` to `float` at the store: which is not an implicit conversion and would be a type error.

## Context-Sensitive Literal Types

`ParseNumberExpression` consults a global `ExpectedLiteralType`:

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

Four sites install a guard:

- **`var` initializers.** `ParseVarStatement` installs the declared type so `var x: int32 = 5` parses `5` as `int32` directly.
- **Assignment RHS.** `ParseAssignmentRight` looks up the type of the already-declared variable and installs it before parsing the right-hand side, so `x = 10` (where `x: int32`) parses `10` as `int32`.
- **Function call arguments.** `ParseNameExpression` (the call parser) installs each parameter's declared type before parsing the corresponding argument expression.
- **`return` expressions.** `ParseReturnStatement` installs the enclosing function's return type so `return 10` inside a `-> int32` function parses `10` as `int32`.

The effect on the IR is that no redundant cast instruction appears:

```llvm
; WITHOUT context guard: var x: float32 = 1.5
; 1.5 parsed as double, then an fptrunc would be needed: but that's not implicit,
; so this would be a type error.

; WITH context guard: var x: float32 = 1.5
; 1.5 parsed as float directly: clean alloca + store, no cast
%x = alloca float
store float 1.500000e+00, ptr %x
```

The guard is a scoped RAII object: when it goes out of scope, `ExpectedLiteralType` reverts to whatever it was before, so nested expressions are not affected.

## Boolean Literals

`True` and `False` are parsed in `ParsePrimary`:

```cpp
case tok_true:
  getNextToken();
  return make_unique<BoolExpressionNode>(true);
case tok_false:
  getNextToken();
  return make_unique<BoolExpressionNode>(false);
```

`BoolExpressionNode` is a new AST class:

```cpp
class BoolExpressionNode : public ExpressionNode {
  bool Val;
public:
  BoolExpressionNode(bool Val) : Val(Val) { setType(ValueType::Bool); }
  Value *codegen() override;
};
```

`BoolExpressionNode::codegen` emits an `i1` constant:

```cpp
Value *BoolExpressionNode::codegen() {
  return ConstantInt::get(Type::getInt1Ty(*TheContext), Val ? 1 : 0);
}
```

```llvm
; True
i1 true

; False
i1 false
```

`Bool` is a distinct type. It is not the result of a comparison widened to an integer: it stays `i1` throughout. Every comparison operator now returns `Bool` / `i1` directly.

## Parsing Type Annotations and Optional Return Types

`ParseTypeToken` consumes the current token if it is a type keyword and returns the corresponding `ValueType`:

```cpp
static ValueType ParseTypeToken() {
  switch (CurrentToken) {
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
    LogErrorExpression("Expected a type");
    return ValueType::Error;
  }
}
```

`tok_float` and `tok_float64` produce distinct `ValueType` values even though both compile to `double`.

`ParseOptionalReturnType` wraps it with a default:

```cpp
static ValueType ParseOptionalReturnType(
    ValueType DefaultType = ValueType::None) {
  if (CurrentToken != tok_arrow)
    return DefaultType;
  getNextToken(); // eat '->'
  return ParseTypeToken();
}
```

The `DefaultType` parameter is the key design decision:

- `ParseFunctionDefinition` calls `ParseOptionalReturnType(ValueType::None)`: unannotated `def` is void.
- `ParseExtern` calls `ParseOptionalReturnType()` (default `Float64`): extern declarations default to `float64` because they are typically C library functions.

`ValueType::None` also serves as a placeholder type for all statement-like AST nodes that produce no meaningful value: `ReturnExpressionNode`, `BlockExpressionNode`, `ForExpressionNode`, `IfStatementNode`, and `VarStatementNode` all call `setType(ValueType::None)` in their constructors, and all override `shouldPrintValue()` to return `false`. This is the same pattern as the single-hierarchy AST design noted in chapter 12: in a clean Stmt/Expr split, statements would carry no type at all. Here, `None` is the convention that means "this node is a statement; its type is not meaningful."

## Parsing Typed Parameters

In Chapter 17, my signature parser accepted bare names:

```cpp
// Chapter 17
while (getNextToken() == tok_name)
  ArgNames.push_back(Name);
```

Chapter 18 requires `name : type` for every parameter:

```cpp
// Chapter 18
vector<pair<string, ValueType>> ArgNames;
getNextToken(); // eat '('
if (CurrentToken != ')') {
  while (true) {
    string ArgName = Name;
    getNextToken(); // eat identifier
    if (CurrentToken != ':')
      return LogErrorP("Parameters require type annotations (e.g., ': int32')");
    getNextToken(); // eat ':'
    ValueType ArgTy = ParseTypeToken();
    if (ArgTy == ValueType::None)
      return LogErrorP("Parameters cannot have None type");
    ArgNames.push_back({ArgName, ArgTy});
    if (CurrentToken == ')') break;
    if (CurrentToken != ',') return LogErrorP("Expected ')' or ','");
    getNextToken(); // eat ','
  }
}
```

`FunctionSignatureNode` now stores `vector<pair<string, ValueType>>` and a `ReturnType` field, with helpers `getParameterType(i)`, `getReturnType()`, `setReturnType()`, and `clone()`.

The same function signature in chapter 17 vs chapter 18:

```llvm
; Chapter 17
define double @add(double %a, double %b) { ... }

; Chapter 18
define i32 @add(i32 %a, i32 %b) { ... }
define i1 @classify(double %x) { ... }
define void @greet() { ... }
```

Every parameter type and every return type now appears literally in the IR rather than being uniformly `double`.

## Tracking Variable Types in Scope

Chapter 17 tracked which variable names were in scope using a `set<string>`:

```cpp
// Chapter 17
static vector<set<string>> VarScopes;
```

Chapter 18 upgrades to `map<string, ValueType>` so the type of each variable is also tracked:

```cpp
// Chapter 18
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

Every `NameExpressionNode` carries its resolved type at parse time. When codegen runs, a load uses `LLVMTypeFor(resolvedType)` for the type operand:

```llvm
; var x: int32: load uses i32
%x_val = load i32, ptr %x

; var r: float32: load uses float
%r_val = load float, ptr %r
```

## var Declarations: Required Type Annotation

Before:

```pyxc
var x = 1.0
var y
```

After:

```pyxc
var x: float64 = 1.0
var y: int32
var a: int8, b: int16   # multiple bindings in one statement
```

The colon-type is mandatory. `ParseVarStatement` reads it:

```cpp
if (CurrentToken != ':')
  return LogErrorExpression(
      "Variable declaration requires a type annotation (e.g., ': int32')");
getNextToken(); // eat ':'
ValueType DeclTy = ParseTypeToken();
if (DeclTy == ValueType::None)
  return LogErrorExpression("Variables cannot have None type");
```

If there's no initializer, I generate a zero constant of the declared type. If there is one, I install an `ExpectedLiteralTypeGuard(DeclTy)` before parsing the initializer expression, then check assignability:

```cpp
{
  ExpectedLiteralTypeGuard Guard(DeclTy);
  Init = ParseExpression();
}
if (!IsAssignable(DeclTy, Init->getType()))
  return LogErrorExpression("Type mismatch in variable initialization");
```

`VarBinding` replaces the old `pair<string, unique_ptr<ExpressionNode>>`:

```cpp
struct VarBinding {
  string Name;
  ValueType Ty;
  unique_ptr<ExpressionNode> Init;
};
```

The generated IR per declaration:

```llvm
; var x: float64 = 1.0
%x = alloca double
store double 1.000000e+00, ptr %x

; var y: int32     (no initializer: zero)
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

```pyxc
# Chapter 17
for var i = 1, i <= n, 1:
    body

# Chapter 18
for var i: int = 1, i <= n, 1:
    body
```

The `: type` annotation follows the loop variable name directly, but only when
the loop declares a fresh variable with `var`. The type is validated:

- Must be numeric (`IsNumericType`).
- The start expression must be assignable to it.
- The step expression must be assignable to it.

`ForExpressionNode` stores `VarType`, and `ForExpressionNode::codegen` uses `LLVMTypeFor(VarType)` for the `alloca` and the increment:

```cpp
if (IsFloatType(VarType))
  NextVar = Builder->CreateFAdd(CurVar, StepVal, "nextvar");
else
  NextVar = Builder->CreateAdd(CurVar, StepVal, "nextvar");
```

For an integer loop variable the alloca and step use the declared integer type:

This is the real output for `for var i: int = 1, i <= 10, 1:`, compiled and read directly:

```llvm
%i = alloca i64, align 8
store i64 1, ptr %i, align 8
br label %loop_cond

loop_cond:
  %i1 = load i64, ptr %i, align 8
  %cmptmp = icmp sle i64 %i1, 10
  br i1 %cmptmp, label %loop_body, label %after_loop

loop_body:
  ; ... body ...
  %i3 = load i64, ptr %i, align 8
  %nextvar = add i64 %i3, 1
  store i64 %nextvar, ptr %i, align 8
  br label %loop_cond
```

`%i` gets renumbered (`%i1`, `%i3`, ...) each time it's reloaded, since LLVM's textual IR gives every value in a function a unique name and `%i` itself is already taken by the `alloca`.

Compare with chapter 17 where `i` would have been `alloca double` and used `fadd` for the increment.

## Explicit Casts

Any type name used as a function call performs a cast:

```pyxc
int32(3.14)     # float64 → int32 (truncates to 3)
float64(42)     # int → float64
int8(x)         # any integer → 8-bit (truncates if needed)
bool(x)         # any value → 0 or 1
float32(n)      # int → float32
```

`ParseCastExpression` is invoked from `ParsePrimary` when the current token is a type keyword:

```cpp
case tok_int:    case tok_int8:   case tok_int16:  case tok_int32:
case tok_int64:  case tok_float:  case tok_float32: case tok_float64:
case tok_bool:
  return ParseCastExpression();
```

`CastExpressionNode::codegen` delegates to `EmitCast`, which emits one of these instructions depending on the type pair:

I compiled a function exercising each of these and read the real instruction names, which differ per conversion rather than sharing one generic hint:

```llvm
; int32(3.14) : float to signed integer (truncates toward zero)
%fptosi = fptosi double 3.140000e+00 to i32
; result: i32 3

; float64(42) : integer to double
%sitofp = sitofp i64 42 to double
; result: double 4.200000e+01

; int8(x) where x: int32 : narrowing truncation
%trunc = trunc i32 %x to i8

; int16(y) where y: int8 : widening sign-extension (explicit cast, same as implicit sext)
%sext = sext i8 %y to i16

; float32(n) where n: int32 : integer to single
%sitofp = sitofp i32 %n to float

; float64(r) where r: float32 : float extension
%fpext = fpext float %r to double

; float32(f) where f: float64 : float truncation
%fptrunc = fptrunc double %f to float

; bool(x) where x: int32 : compare against zero
%tobool = icmp ne i32 %x, 0
; result: i1

; bool(f) where f: float64 : compare float against zero
%tobool = fcmp one double %f, 0.000000e+00
; result: i1
```

## Mapping Types to LLVM IR and Zero Values

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

## Checking Whether a Type Can Convert Implicitly

`IsAssignable(Dest, Src)` determines whether a value of type `Src` can appear where `Dest` is expected without an explicit cast:

```cpp
static bool IsAssignable(ValueType Dest, ValueType Src) {
  if (Dest == Src)
    return true;
  if ((Dest == ValueType::Float && Src == ValueType::Float64) ||
      (Dest == ValueType::Float64 && Src == ValueType::Float))
    return true;
  if (IsFloatType(Dest) && IsFloatType(Src)) {
    unsigned DestBits = LLVMTypeFor(Dest)->getScalarSizeInBits();
    unsigned SrcBits = LLVMTypeFor(Src)->getScalarSizeInBits();
    if (DestBits >= SrcBits)
      return true;
  }
  if (IsIntType(Dest) && IsIntType(Src) && CanWidenInt(Src, Dest))
    return true;
  if (IsFloatType(Dest) && IsIntType(Src))
    return true;
  return false;
}
```

The third rule is what actually lets `float32` widen into `float64`: `Float`/`Float64` are both 64 bits so the special-case above it already covers them, but this is the general rule that also lets a plain `float32` value flow into a `float64` destination (`DestBits >= SrcBits`, 64 ≥ 32). The last rule covers `IsFloatType(Dest)`: any integer can widen to any float type (`Float`, `Float32`, or `Float64`).

`CanWidenInt` determines integer widening legality:

```cpp
static bool CanWidenInt(ValueType From, ValueType To) {
  if (From == To)
    return true;
  if (IsIntType(From) && IsIntType(To)) {
    unsigned FromBits = LLVMTypeFor(From)->getIntegerBitWidth();
    unsigned ToBits = LLVMTypeFor(To)->getIntegerBitWidth();
    return FromBits <= ToBits;
  }
  return false;
}
```

Instead of comparing types by name, I ask LLVM for each type's actual bit width and compare those. `LLVMTypeFor(ValueType::Int)` doesn't return a fixed size: it asks the target's data layout for its pointer width, so `Int` is whatever width the platform uses (64 bits on the machines I've been building on, but this isn't guaranteed). Every other integer type (`Int8`, `Int16`, `Int32`, `Int64`) maps to its fixed LLVM width. Widening is legal whenever the source width is no greater than the destination width, `Int` included.

Each allowed implicit conversion and the IR it produces (assuming a target where `Int` is 64 bits, matching `Int64`):

| Assignment | Allowed? | IR emitted |
|-----------|----------|------------|
| `var x: int16 = int8_val` | Yes | `sext i8 %v to i16` |
| `var x: int32 = int16_val` | Yes | `sext i16 %v to i32` |
| `var x: int64 = int32_val` | Yes | `sext i32 %v to i64` |
| `var x: int = int32_val` | Yes, since `Int` is at least as wide as `Int32` here | `sext i32 %v to i64` |
| `var x: int64 = int_val` | Yes, since `Int` and `Int64` are the same width here | *(no instruction: same IR type)* |
| `var x: float64 = int_val` | Yes | `sitofp i64 %v to double` |
| `var x: float32 = int_val` | Yes | `sitofp i64 %v to float` |
| `var x: float = float64_val` | Yes | *(no instruction: same IR type)* |
| `var x: float64 = float32_val` | Yes | `fpext float %v to double` |
| `var x: float32 = float64_val` | No | type error (narrowing) |
| `var x: int32 = int64_val` | No | type error |
| `var x: bool = int_val` | No | type error |

On a target where `Int` is narrower, say 32 bits, the `Int`/`Int64` rows flip: `int64_val` could no longer implicitly narrow into `Int` (unchanged, that direction was always disallowed), but `int_val` widening into `Int64` would now emit a `sext` instead of passing through unchanged. The rule itself (`FromBits <= ToBits`) doesn't change; only which concrete widths it's comparing does.

`EmitImplicitCast` is called by codegen whenever one of the allowed cases applies. This is the real function, and it's narrower than `IsAssignable` above it:

```cpp
static Value *EmitImplicitCast(Value *V, ValueType From, ValueType To) {
  if (From == To)
    return V;
  if (IsFloatType(From) && IsFloatType(To)) {
    unsigned FromBits = LLVMTypeFor(From)->getScalarSizeInBits();
    unsigned ToBits = LLVMTypeFor(To)->getScalarSizeInBits();
    if (FromBits == ToBits)
      return V;
    if (FromBits < ToBits)
      return Builder->CreateFPExt(V, LLVMTypeFor(To), "fpext");
    return nullptr;
  }
  if (IsIntType(From) && IsIntType(To) && CanWidenInt(From, To)) {
    unsigned FromBits = LLVMTypeFor(From)->getIntegerBitWidth();
    unsigned ToBits = LLVMTypeFor(To)->getIntegerBitWidth();
    if (FromBits == ToBits)
      return V;
    return Builder->CreateSExt(V, LLVMTypeFor(To), "sext");
  }
  if (IsIntType(From) && IsFloatType(To))
    return Builder->CreateSIToFP(V, LLVMTypeFor(To), "sitofp");
  return nullptr;
}
```

The final branch handles every integer-to-float conversion accepted by `IsAssignable`; `LLVMTypeFor(To)` selects either `float` or `double`. Also note that `Float`/`Float64` interchangeability isn't special-cased here as `IsAssignable` implies: it falls out naturally from the `FromBits == ToBits` check in the float-to-float branch, since both are 64 bits.

## Binary Operators: Type-Aware Arithmetic

`GetBinaryResultType` decides the type of a binary expression at parse time:

```cpp
static ValueType GetBinaryResultType(int Op, ValueType L, ValueType R) {
  if (IsArithmeticOp(Op)) {
    if (!IsNumericType(L) || !IsNumericType(R))
      return ValueType::Error;
    // Float and Float64 can be mixed: result is Float64
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
  return ValueType::Error;
}
```

`BinaryExpressionNode::codegen` implicitly casts both operands to the result type then selects float vs integer instructions. This is the arithmetic case of its `switch (Operator)`; comparisons are a separate case with their own type-resolution logic, described below:

```cpp
case '+':
case '-':
case '*': {
  L = EmitImplicitCast(L, LType, getType());
  R = EmitImplicitCast(R, RType, getType());
  if (!L || !R)
    return LogErrorV("Type mismatch in arithmetic");
  if (IsFloatType(getType())) {
    if (Operator == '+')
      return Builder->CreateFAdd(L, R, "addtmp");
    if (Operator == '-')
      return Builder->CreateFSub(L, R, "subtmp");
    return Builder->CreateFMul(L, R, "multmp");
  }
  if (Operator == '+')
    return Builder->CreateAdd(L, R, "addtmp");
  if (Operator == '-')
    return Builder->CreateSub(L, R, "subtmp");
  return Builder->CreateMul(L, R, "multmp");
}
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

; int32 + float32 → float32   (int32 widens into float32, same as float64)
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

Comparisons return `i1` directly: there is no `UIToFP` widening to `double` as there was in chapter 17. The old pattern emerged from having only `double`; now each comparison produces a proper `Bool`.

## Explicit Conversions: The Full Table

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
| float ↔ float64 | either | *(no instruction: same IR type)* |
| any int | bool | `icmp ne iN %v, 0` |
| any float | bool | `fcmp one double/float %v, 0.0` |

## Void (None) Functions

A `def` without `->` produces a void function:

```pyxc
def greet():        # return type = None (void)
    printd(42.0)
```

Explicit `-> None` is identical at the IR level: it is documentary only:

```pyxc
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

The `call` result is present in the IR (LLVM always names it) but no `ret` of that value follows: `ret void` terminates the block.

### Tracking the Enclosing Function's Return Type

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

`ParseFunctionDefinition` instantiates a `ReturnTypeGuard` before parsing the body. `ParseReturnStatement` also installs an `ExpectedLiteralTypeGuard(CurrentFunctionReturnType)` before parsing the return value, so bare integer or float literals in `return` statements are given the function's return type directly. Then it validates:

```cpp
// bare 'return' (no value)
if (CurrentToken == tok_eol || CurrentToken == tok_dedent || CurrentToken == tok_eof) {
  if (CurrentFunctionReturnType != ValueType::None)
    return LogErrorExpression("Return value required");
  return make_unique<ReturnExpressionNode>(nullptr);
}
// return with value
if (CurrentFunctionReturnType == ValueType::None)
  return LogErrorExpression("cannot return a value from a None function");
if (!IsAssignable(CurrentFunctionReturnType, Expr->getType()))
  return LogErrorExpression("cannot return X from function returning Y");
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
  Stmt = make_unique<ReturnExpressionNode>(std::move(Stmt));

auto Proto = make_unique<FunctionSignatureNode>(
    FnName, vector<pair<string, ValueType>>(), CurLoc, RetTy);
```

```llvm
; calling greet() at top level: wrapper is void
define void @__pyxc.toplevel.3() {
entry:
  call void @greet()
  ret void
}

; evaluating 1 + 2 at top level: wrapper returns int64
define i64 @__pyxc.toplevel.4() {
entry:
  ret i64 3
}
```

`CallExpressionNode` overrides `shouldPrintValue()`: void calls are silently discarded in the REPL without printing anything.

## Wrapping main for the Native ABI

When `--emit exe` is used, the OS needs a C-ABI `int main()` entry point, but I don't want to force the user to write `def main() -> int32` specifically; `int` (the platform default) or plain `def main()` (void) should both work. So before wrapping anything, I validate the return type explicitly and reject anything else:

```cpp
auto MainIt = FunctionSignatures.find("main");
if (MainIt != FunctionSignatures.end()) {
  ValueType MainRet = MainIt->second->getReturnType();
  if (MainRet != ValueType::Int && MainRet != ValueType::None) {
    fprintf(stderr, "Error: main() must return int or None\n");
    HadError = true;
    return false;
  }
}
```

I found this out by trying `def main() -> int32` myself: it's rejected. Only `int` and `None` are accepted, since those are the two shapes the wrapper below actually knows how to handle.

```cpp
if (auto *UserMain = TheModule->getFunction("main")) {
  // Always wrap user's main() in an int32 main() so the OS entry point has
  // the correct C ABI. The user-defined main() must return int or None.
  UserMain->setName("__pyxc.user_main");
  FunctionType *FT = FunctionType::get(Type::getInt32Ty(*TheContext), false);
  Function *Wrapper = Function::Create(FT, Function::ExternalLinkage, "main",
                                       TheModule.get());
  BasicBlock *BB = BasicBlock::Create(*TheContext, "entry", Wrapper);
  IRBuilder<> TmpB(BB);
  if (UserMain->getReturnType()->isIntegerTy()) {
    Value *Ret = TmpB.CreateCall(UserMain);
    if (!UserMain->getReturnType()->isIntegerTy(32))
      Ret = TmpB.CreateTrunc(Ret, Type::getInt32Ty(*TheContext));
    TmpB.CreateRet(Ret);
  } else {
    TmpB.CreateCall(UserMain);
    TmpB.CreateRet(ConstantInt::get(Type::getInt32Ty(*TheContext), 0));
  }
}
```

The `isIntegerTy(32)` check isn't about accepting `int32` specifically, `int` itself might already be 32 or 64 bits depending on the host. On my 64-bit host, `int` resolves to `i64`, so `__pyxc.user_main`'s return has to be truncated down to the `i32` the OS actually expects. I confirmed this directly for `def main() -> int:`:

```llvm
define i64 @__pyxc.user_main() {
  ; ... user code ...
  ret i64 0
}

define i32 @main() {
entry:
  %0 = call i64 @__pyxc.user_main()
  %1 = trunc i64 %0 to i32
  ret i32 %1
}
```

For a void `def main():`, the wrapper skips the truncation path entirely and just substitutes a literal `0`:

```llvm
define void @__pyxc.user_main() {
entry:
  ret void
}

define i32 @main() {
entry:
  call void @__pyxc.user_main()
  ret i32 0
}
```

The wrapper is what the OS and C runtime actually call. The user-visible `main` name is preserved by renaming the user's own function to `__pyxc.user_main` first, then building the wrapper under the name `main` that now points at the real entry point.

## JIT Dispatch: Type-Switched Invocation

Before chapter 17, the JIT always called the anonymous top-level function as `double (*)()`. Now the return type determines both the function pointer type and the print format:

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
- Integer types print as `%lld`: decimal integer notation, not floating-point.
- `Bool` prints as `True` or `False`: matching the keyword spelling.
- `None` (void) produces no output.

## Debug Info: Per-Type DWARF Descriptors

In Chapter 17 I had one `DblDIType` for everything. Now I need a descriptor per type:

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

The void type uses `createUnspecifiedType("None")`: the correct DWARF tag `DW_TAG_unspecified_type` for a type with no representation.

## HadError and Exit Codes

Chapter 17 always returned `0`. File-mode programs with type errors would print to stderr but exit cleanly, making shell scripts and test harnesses oblivious to failures.

Chapter 18 adds a global `HadError` flag set by every `LogErrorExpression` call. File-mode loops check it after parsing:

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

Here's a small program that exercises the type system, and the real IR it produces, compiled and read directly rather than written from memory:

```pyxc
extern def printd(x: float64) -> float64

def add(a: int32, b: int32) -> int32:
    return a + b

def main() -> int:
    var x: int32 = add(10, 5)
    var y: float64 = float64(x)
    printd(y)
    return 0
```

I use `-> int` for `main`, not `-> int32`; that validation check above rejects anything else.

```llvm
define i32 @add(i32 %a, i32 %b) {
entry:
  %b2 = alloca i32, align 4
  %a1 = alloca i32, align 4
  store i32 %a, ptr %a1, align 4
  store i32 %b, ptr %b2, align 4
  %a3 = load i32, ptr %a1, align 4
  %b4 = load i32, ptr %b2, align 4
  %addtmp = add i32 %a3, %b4
  ret i32 %addtmp
}

define i64 @__pyxc.user_main() {
entry:
  %y = alloca double, align 8
  %x = alloca i32, align 4
  %calltmp = call i32 @add(i32 10, i32 5)
  store i32 %calltmp, ptr %x, align 4
  %x1 = load i32, ptr %x, align 4
  %sitofp = sitofp i32 %x1 to double
  store double %sitofp, ptr %y, align 8
  %y2 = load double, ptr %y, align 8
  %calltmp3 = call double @printd(double %y2)
  ret i64 0
}

define i32 @main() {
entry:
  %0 = call i64 @__pyxc.user_main()
  %1 = trunc i64 %0 to i32
  ret i32 %1
}
```

Two things worth noticing that I wouldn't have guessed without actually compiling this. First, `add`'s parameters get their own `alloca`s and are loaded back before the addition, even though nothing about the source needs that indirection, that's `IRBuilder<NoFolder>` plus the empty `-O0` pass list from earlier in this chapter: nothing is promoting those stack slots to registers yet, so every parameter round-trips through memory exactly like a `var` would. Second, `__pyxc.user_main` itself returns `i64`, not `i32`, because `int` resolved to the platform's 64-bit width on the machine I compiled this on; the `trunc` in `main` is doing real work here, not just defensive code.

Before Chapter 17, every value in this program would have been `double`, with no `alloca`/`load` distinction to speak of. Now `add` uses `i32` throughout, the `sitofp` appears exactly once and only where the source explicitly asked for it with `float64(x)`, and the path from a typed `main` down to the OS-facing `i32 @main` is fully explicit instead of assumed.

## Known Limitations

**`None` cannot be used as a variable type.** `var x: None` is rejected. `None` is only valid as a return type annotation.

**`Int` does not widen to fixed-size integers.** `Int` (pointer-width) can widen to `Int64`, but not to `Int32` or smaller: even on a 32-bit host where they would have the same width. Use an explicit cast when crossing `Int`/`Int32` boundaries.

**`float32 + float64` is a type error.** The two float sizes are not interchangeable in binary operations: only `float` and `float64` are. Use an explicit cast: `float64(x) + y`.

## Try It

**Boolean literals**

```pyxc
ready> True
True
ready> False
False
ready> True == False
False
```

**REPL prints by type**

```pyxc
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

```pyxc
# mismatch.pyxc
def add(a: int32, b: int32) -> int32:
    return a + b
add(1.0, 2.0)  # Error: argument 1 expects int32
```

```bash
pyxc mismatch.pyxc  # exits with status 1
```

**Mixed int sizes: widening is automatic**

```pyxc
var a: int8 = 10
var b: int16 = 200
var c: int32 = a + b   # int8 widens to int16, result int16 widens to int32
```

**Explicit cast round-trip**

```pyxc
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
cd code/chapter-18
cmake -S . -B build && cmake --build build
echo "var x: int32 = 7" | ./build/pyxc
```

## What's Next

[Chapter 19](chapter-19.md) adds unsigned integer types.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.
