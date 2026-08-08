---
description: "Add fixed-size stack arrays: declare int[4], initialize with [1, 2, 3, 4], index with arr[i], and pass them anywhere a pointer is expected."
---
# 24. pyxc: Arrays

## Where We Are

[Chapter 23](chapter-23.md) gave me type aliases, but the type system still can't express "a fixed number of these, sitting next to each other." For that I need arrays: `int[4]`, allocated on the stack, indexed with `arr[i]`.

After this chapter:

```pyxc
extern def printd(x: float64)

def main() -> int:
  var scores: int[4] = [10, 20, 30, 40]
  printd(float64(scores[2]))
  return 0
```

```text
30.000000
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-24
```

## One New `ValueType`, One New Node

A single enum value covers every array regardless of element type:

```cpp
enum class ValueType {
  // ...existing values...
  Array,
  // ...
};
```

An array literal needs its own node, since it's not a name, a number, or anything else I already have a class for:

```cpp
class ArrayLiteralExpressionNode : public ExpressionNode {
  vector<unique_ptr<ExpressionNode>> Elements;

public:
  ArrayLiteralExpressionNode(vector<unique_ptr<ExpressionNode>> Elements,
                      const string &ArrayTypeInfo)
      : Elements(std::move(Elements)) {
    setType(ValueType::Array, ArrayTypeInfo);
  }
  const vector<unique_ptr<ExpressionNode>> &getElements() const { return Elements; }
  Value *codegen() override;
};
```

## Encoding Element Type and Count Together

Every pointer in pyxc already carries its pointee type through the struct-name slot every `ExpressionNode` has, encoded as a string. An array needs the same thing plus one more field: how many elements. I extend the same encoding scheme with a third colon-separated part:

```text
"<ElemTypeInt>:<ElemStructName>:<Count>"
```

| Type | Encoding |
|------|----------|
| `int[4]` | `"1::4"` |
| `float64[3]` | `"8::3"` |
| `Point[2]` | `"10:Point:2"` |

```cpp
static string EncodeArrayType(ValueType ElemType, const string &ElemStructName,
                              uint64_t Count) {
  return std::to_string(static_cast<int>(ElemType)) + ":" + ElemStructName +
         ":" + std::to_string(Count);
}
```

Decoding reverses it, splitting on the first and last `:` so the middle piece (the struct name, which could itself be empty) doesn't have to be delimiter-free:

```cpp
static bool DecodeArrayType(const string &Encoded, ValueType &ElemType,
                            string &ElemStructName, uint64_t &Count);
```

I need one more helper: can I pass this array where a pointer is expected? An array decays to a pointer of the same element type, the same way a C array decays to `T *`, so the check just decodes both sides and compares element types:

```cpp
static bool ArrayDecaysToPointerType(const string &ArrayInfo,
                                     const string &PointerInfo);
```

And since an array size is a plain decimal literal I read out of `NumberLiteral` (a string) rather than something the lexer already parsed as a number, I need a small, careful integer parser rather than reusing the float-tolerant number path:

```cpp
static bool ParseUnsignedDecimal(const string &Text, uint64_t &Out) {
  if (Text.empty())
    return false;
  uint64_t V = 0;
  for (char C : Text) {
    if (C < '0' || C > '9')
      return false;
    uint64_t D = static_cast<uint64_t>(C - '0');
    if (V > (std::numeric_limits<uint64_t>::max() - D) / 10)
      return false;
    V = V * 10 + D;
  }
  Out = V;
  return true;
}
```

The overflow check matters here specifically because array size feeds directly into `ArrayType::get`; I'd rather reject `int[99999999999999999999]` at parse time with a clear error than let it wrap around into something silently wrong.

## Teaching `ParseTypeToken` About `[N]`

Before this chapter, `ParseTypeToken` returned the moment it recognized a base type; there was nowhere left to check for a trailing `[4]`. I have it collect the base type into a local instead of returning immediately, so I can look at what follows before deciding what to return:

```cpp
if (CurrentToken == '[') {
  if (BaseType == ValueType::None)
    return LogError("Arrays of None are not allowed"), ValueType::Error;
  if (BaseType == ValueType::Array)
    return LogError("Nested array types are not supported"), ValueType::Error;
  getNextToken(); // eat '['
  if (CurrentToken != tok_number || NumberIsFloat)
    return LogError("Array size must be an integer literal"),
           ValueType::Error;
  uint64_t Count = 0;
  if (!ParseUnsignedDecimal(NumberLiteral, Count))
    return LogError("Invalid array size"), ValueType::Error;
  if (Count == 0)
    return LogError("Array size must be > 0"), ValueType::Error;
  getNextToken(); // eat number
  if (CurrentToken != ']')
    return LogError("Expected ']' after array size"), ValueType::Error;
  getNextToken(); // eat ']'
  if (StructName)
    *StructName = EncodeArrayType(BaseType, BaseStructName, Count);
  if (CurrentToken == '[')
    return LogError("Nested arrays are not supported"), ValueType::Error;
  return ValueType::Array;
}

if (StructName)
  *StructName = BaseStructName;
return BaseType;
```

Two different situations both count as "nested array," and I reject both: an alias whose *own* underlying type is already an array (`BaseType == ValueType::Array`), and a literal double suffix like `int[4][2]` (the trailing check after the closing `]`). I verified both:

```pyxc
var m: int[4][2]
```

```text
Error (Line 2, Column 16): Nested arrays are not supported
```

```pyxc
def f(n: int) -> int:
  var buf: int[n]
```

```text
Error (Line 2, Column 16): Array size must be an integer literal
```

## Array Literals Need to Know What They're Building

`[10, 20, 30, 40]` carries no type information of its own; a bare list of numbers could be `int[4]` or `float64[4]` or something else entirely. I already have a mechanism for this: `ExpectedLiteralType`, a global that every context expecting a literal, a `var` initializer, a return statement, a function argument, sets before parsing the expression. I just extend it to also carry a struct name, since "expected type" now sometimes means "expected array of a specific element type and count," not just a bare `ValueType`:

```cpp
struct ExpectedLiteralTypeGuard {
  ValueType Saved;
  string SavedStruct;
  ExpectedLiteralTypeGuard(ValueType Type, const string &StructName = "")
      : Saved(ExpectedLiteralType), SavedStruct(ExpectedLiteralStructName) {
    ExpectedLiteralType = Type;
    ExpectedLiteralStructName = StructName;
  }
  ~ExpectedLiteralTypeGuard() {
    ExpectedLiteralType = Saved;
    ExpectedLiteralStructName = SavedStruct;
  }
};
```

`ReturnTypeGuard` gets the identical treatment, for the same reason: a function returning `int[4]` needs that full context available while its body parses.

With that in place, parsing the literal itself is straightforward: read the expected element type and count out of the guard, then parse exactly that many elements:

```cpp
static unique_ptr<ExpressionNode> ParseArrayLiteralExpression() {
  if (ExpectedLiteralType != ValueType::Array)
    return LogError("Array literal requires an expected array type");
  ValueType ElemType = ValueType::Error;
  string ElemStructName;
  uint64_t Count = 0;
  if (!DecodeArrayType(ExpectedLiteralStructName, ElemType, ElemStructName,
                       Count))
    return LogError("Invalid expected array type");

  getNextToken(); // eat '['
  vector<unique_ptr<ExpressionNode>> Elements;
  if (CurrentToken != ']') {
    while (true) {
      ExpectedLiteralTypeGuard Guard(ElemType, ElemStructName);
      auto E = ParseExpression();
      if (!E)
        return nullptr;
      if (!IsAssignable(ElemType, E->getType()))
        return LogError("Array literal element type mismatch");
      if ((ElemType == ValueType::Pointer || ElemType == ValueType::Array ||
           ElemType == ValueType::Struct) &&
          ElemStructName != E->getStructName())
        return LogError("Array literal element type mismatch");
      Elements.push_back(std::move(E));
      if (CurrentToken == ']')
        break;
      if (CurrentToken != ',')
        return LogError("Expected ']' or ',' in array literal");
      getNextToken();
    }
  }
  getNextToken(); // eat ']'
  if (Elements.size() != Count)
    return LogError("Array literal element count mismatch");
  return make_unique<ArrayLiteralExpressionNode>(std::move(Elements),
                                          ExpectedLiteralStructName);
}
```

I check the element count only after parsing every element, not as I go, since a short-circuit "too many elements" error the moment I see one extra value would be less useful than parsing the whole literal and reporting the real mismatch:

```pyxc
var a: int[4] = [1, 2, 3]
```

```text
Error (Line 2, Column 28): Array literal element count mismatch
```

## Codegen: Building the Literal as a Register Value

An array literal isn't stored anywhere until something assigns it, so its own codegen builds a pure SSA aggregate value, one element at a time, with `insertvalue`, no `alloca` involved:

```cpp
Value *ArrayLiteralExpressionNode::codegen() {
  ValueType ElemType = ValueType::Error;
  string ElemStructName;
  uint64_t Count = 0;
  if (!DecodeArrayType(getStructName(), ElemType, ElemStructName, Count))
    return LogErrorV("Invalid array literal type");
  if (Elements.size() != Count)
    return LogErrorV("Array literal element count mismatch");

  auto *ArrTy = dyn_cast<ArrayType>(LLVMTypeFor(getType(), getStructName()));
  if (!ArrTy)
    return LogErrorV("Invalid array LLVM type");

  Value *Agg = UndefValue::get(ArrTy);
  for (size_t I = 0; I < Elements.size(); ++I) {
    Value *Elem = Elements[I]->codegen();
    if (!Elem)
      return nullptr;
    Elem = EmitImplicitCast(Elem, Elements[I]->getType(), ElemType);
    if (!Elem)
      return nullptr;
    Agg = Builder->CreateInsertValue(Agg, Elem, {static_cast<unsigned>(I)},
                                     "arr.ins");
  }
  return Agg;
}
```

Whatever consumes this value, a `var` initializer, is what actually stores it into stack memory; this function never allocates anything itself.

## Indexing: Reusing Pointer Arithmetic Instead of Duplicating It

I already had a helper, `BuildIndexElementPtr`, that computes the address `arr[i]` should read from or write to for pointer values. Rather than write a second, parallel version for arrays, I teach the *first step* of that helper to also accept an array, producing an equivalent starting pointer, then let everything after that step stay exactly as it was:

```cpp
static Value *BuildIndexElementPtr(IndexExpressionNode *IdxExpr) {
  ValueType PtrType = ValueType::Error;
  string PtrStructName;
  Value *BasePtr = nullptr;
  if (IdxExpr->getFieldPath().empty()) {
    auto It = NamedValues.find(IdxExpr->getBaseName());
    if (It != NamedValues.end() && It->second) {
      PtrType = NamedValueTypes[IdxExpr->getBaseName()];
      PtrStructName = NamedValueStructNames[IdxExpr->getBaseName()];
      if (PtrType == ValueType::Pointer) {
        BasePtr = Builder->CreateLoad(LLVMTypeFor(ValueType::Pointer),
                                      It->second, "ptrload");
      } else if (PtrType == ValueType::Array) {
        Value *Zero = ConstantInt::get(Type::getInt64Ty(*TheContext), 0);
        auto *ArrTy = LLVMTypeFor(PtrType, PtrStructName);
        BasePtr = Builder->CreateInBoundsGEP(ArrTy, It->second, {Zero, Zero},
                                             "arraydecay");
      }
    } else if (auto *GV = GetGlobalVariable(IdxExpr->getBaseName())) {
      PtrType = GlobalVarTypes[IdxExpr->getBaseName()];
      PtrStructName = GlobalVarStructTypes[IdxExpr->getBaseName()];
      if (PtrType == ValueType::Pointer) {
        BasePtr =
            Builder->CreateLoad(LLVMTypeFor(ValueType::Pointer), GV, "ptrload");
      } else if (PtrType == ValueType::Array) {
        Value *Zero = ConstantInt::get(Type::getInt64Ty(*TheContext), 0);
        auto *ArrTy = LLVMTypeFor(PtrType, PtrStructName);
        BasePtr =
            Builder->CreateInBoundsGEP(ArrTy, GV, {Zero, Zero}, "arraydecay");
      }
    }
  } else {
    BasePtr = LoadPointerValue(IdxExpr->getBaseName(), IdxExpr->getFieldPath(),
                               PtrType, PtrStructName);
  }
  if (!BasePtr)
    return LogErrorV("Indexing requires a pointer or array value");
  Value *IdxVal = IdxExpr->getIndex()->codegen();
  if (!IdxVal)
    return nullptr;
  if (!IsIntType(IdxExpr->getIndex()->getType()))
    return LogErrorV("Pointer index must be an integer");
  if (IdxExpr->getIndex()->getType() != ValueType::Int64) {
    IdxVal = EmitImplicitCast(IdxVal, IdxExpr->getIndex()->getType(),
                              ValueType::Int64);
    if (!IdxVal)
      return LogErrorV("Index must be an integer");
  }
  return Builder->CreateInBoundsGEP(
      LLVMTypeFor(IdxExpr->getType(), IdxExpr->getStructName()), BasePtr,
      IdxVal, "elemptr");
}
```

For a pointer, `BasePtr` is just whatever address it holds, loaded normally. For an array, there's no pointer stored anywhere to load; the array's storage *is* the value. So I get an equivalent pointer the same way C does, a GEP with two zero indices that steps through the alloca to the array, then through the array to its first element. Once `BasePtr` exists, both cases fall through to the exact same final `getelementptr`, offset by the index. I confirmed this produces two separate GEP instructions, not one combined one, by compiling and reading the IR myself rather than assuming:

```llvm
%arraydecay = getelementptr inbounds [4 x i64], ptr %a1, i64 0, i64 0
%elemptr = getelementptr inbounds i64, ptr %arraydecay, i64 0
%elemload = load i64, ptr %elemptr, align 8
```

## Using an Array Where a Pointer Is Expected

Indexing isn't the only place an array needs to become a pointer. Passing an array variable as a whole, to a function expecting `ptr[T]`, needs the same decay. `NameExpressionNode::codegen` gets a small local lambda for exactly this:

```cpp
auto DecayArray = [&](Value *BasePtr) -> Value * {
  if (getType() != ValueType::Array)
    return BasePtr;
  auto *ArrTy = LLVMTypeFor(getType(), getStructName());
  Value *Zero = ConstantInt::get(Type::getInt64Ty(*TheContext), 0);
  return Builder->CreateInBoundsGEP(ArrTy, BasePtr, {Zero, Zero},
                                    "arraydecay");
};
```

Loading an array by name would otherwise hand back the entire aggregate, which is only meaningful as something to `store`, not something to pass around as a value. Function-call argument checking uses `ArrayDecaysToPointerType` to allow this specific case through even though `ptr[int]` and `int[4]` aren't the same `ValueType`:

```pyxc
def sum4(p: ptr[int]) -> int:
  return p[0] + p[1] + p[2] + p[3]

def main() -> int:
  var scores: int[4] = [10, 20, 30, 40]
  return sum4(scores)
```

I ran this rather than take the decay on faith; it returns `100`.

## Build and Run

```bash
cd code/chapter-24
cmake -S . -B build && cmake --build build
```

## Try It

### Declare, initialize, index

```pyxc
extern def printd(x: float64)

def main() -> int:
  var scores: int[4] = [10, 20, 30, 40]
  printd(float64(scores[2]))
  return 0
```

```text
30.000000
```

### An array parameter, indexed in the body

```pyxc
def sum4(a: int[4]) -> int:
  return a[0] + a[1] + a[2] + a[3]
```

```bash
pyxc --emit llvm-ir -o out.ll program.pyxc
grep -A3 'define.*sum4' out.ll
```

```llvm
define i64 @sum4([4 x i64] %a) {
entry:
  %a1 = alloca [4 x i64], align 8
  store [4 x i64] %a, ptr %a1, align 8
```

### An array decaying to a pointer argument

```pyxc
extern def printd(x: float64)

def sum4(p: ptr[int]) -> int:
  return p[0] + p[1] + p[2] + p[3]

def main() -> int:
  var scores: int[4] = [10, 20, 30, 40]
  printd(float64(sum4(scores)))
  return 0
```

```text
100.000000
```

### Element count mismatch

```pyxc
def main() -> int:
  var a: int[4] = [1, 2, 3]
  return 0
```

```text
Error (Line 2, Column 28): Array literal element count mismatch
```

## Known Limitations

**Size must be a literal.** `var buf: int[n]` is rejected; the element count has to be a constant integer known while parsing, not a variable.

**No nested arrays.** `int[4][2]` isn't valid syntax, whether written directly or through an alias whose underlying type is already an array. A struct with multiple array fields is the way to get a 2D layout.

**No heap arrays.** Everything in this chapter lives on the stack. For dynamically sized or long-lived data, [Chapter 21](chapter-21.md)'s `malloc` and `ptr[T]` are still what I reach for.

**Struct fields can't be arrays yet.** Struct fields are still limited to scalar, pointer, and struct types.

**No pointer arithmetic directly on an array.** Indexing works; adding an integer to an array variable itself doesn't. `addr(arr[i])` gets a pointer to a specific element if I need one.

## Grammar

[pyxc.ebnf](https://github.com/alankarmisra/pyxc-llvm-tutorial/blob/main/code/chapter-24/pyxc.ebnf)

```ebnf
program         = [ end-of-lines ] [ top-level-item { end-of-lines top-level-item } ] [ end-of-lines ] ;
end-of-lines            = end-of-line { end-of-line } ;
top-level-item             = type-alias | struct-definition | function-definition | decorated-function-definition | external | top-level-expression ;
type-alias       = "type" name "=" type ;
struct-definition       = "struct" name ":" end-of-lines struct-block ;
struct-block     = indent field-declaration { end-of-lines field-declaration } dedent ;
field-declaration       = name ":" type ;
function-definition      = "def" function-signature [ "->" type ] ":" ( simple-statement | end-of-lines block ) ;
(* If the return type is omitted, it defaults to None. *)
decorated-function-definition    = binary-decorator end-of-lines "def" binary-operator-signature [ "->" type ] ":" ( simple-statement | end-of-lines block )
                | unary-decorator  end-of-lines "def" unary-operator-signature  [ "->" type ] ":" ( simple-statement | end-of-lines block ) ;
binary-decorator = "@" "binary" "(" integer ")" ;
unary-decorator  = "@" "unary" ;
binary-operator-signature = custom-operator-character "(" typed-parameter "," typed-parameter ")" ;
unary-operator-signature  = custom-operator-character "(" typed-parameter ")" ;
external        = "extern" "def" function-signature [ "->" type ] ;
top-level-expression    = expression ;
function-signature       = name "(" [ typed-parameter { "," typed-parameter } ] ")" ;
typed-parameter      = name ":" type ;
if-statement          = "if" expression ":" suite
                [ end-of-lines "else" ":" suite ] ;
for-statement         = "for"
                  ( "var" name ":" type | name )
                  "=" expression "," expression "," expression ":" suite ;
variable-statement         = "var" variable-binding { "," variable-binding } ;
assignment-statement      = lvalue "=" expression ; (* assignment is a statement here *)
simple-statement      = return-statement | variable-statement | assignment-statement | expression ;
compound-statement    = if-statement | for-statement ;
statement       = simple-statement | compound-statement ;
suite           = simple-statement | compound-statement | end-of-lines block ;
return-statement      = "return" [ expression ] ;
statement-separator = end-of-lines | BLOCK_END ;
block = indent statement { statement-separator statement } dedent ;
expression      = unary-expression binary-operator-right ;
binary-operator-right        = { binary-operator unary-expression } ;
lvalue          = name | field-access | index-expression ;
variable-binding      = name ":" type [ "=" expression ] ;
unary-expression       = unary-operator unary-expression | primary ;
unary-operator         = "-" | user-defined-unary-operator ;
primary         = cast-expression | sizeof-expression | address-expression | array-literal | string-literal | name-expression | field-access | index-expression | number-expression | boolean-literal | parenthesized-expression ;   (* changed: array-literal added *)
cast-expression        = cast-type "(" expression ")" ;
sizeof-expression      = "sizeof" "(" type ")" ;
address-expression        = "addr" "(" lvalue ")" ;
name-expression  = name | call-expression ;
call-expression        = name "(" [ expression { "," expression } ] ")" ;
field-access     = name "." name { "." name } ;
index-expression       = name "[" expression "]" ;
number-expression      = number ;
array-literal    = "[" [ expression { "," expression } ] "]" ;         (* new *)
string-literal   = "\"" { ? any char except " and newline ? | escape } "\"" ;
escape          = "\\" ( "\\" | "\"" | "n" | "t" | "0" ) ;
parenthesized-expression       = "(" expression ")" ;
binary-operator        = builtin-binary-operator | user-defined-binary-operator ;
indent          = INDENT ;
dedent          = DEDENT ;

builtin-binary-operator = "+" | "-" | "*" | "<" | "<=" | ">" | ">=" | "==" | "!=" ;
user-defined-binary-operator = ? any operator-character defined as a custom binary operator ? ;
user-defined-unary-operator  = ? any operator-character defined as a custom unary operator ? ;
custom-operator-character    = ? any operator-character that is not "-" or a builtin-binary-operator,
                    and not already defined as a custom operator ? ;
operator-character          = ? any single ASCII punctuation character ? ;
name      = (letter | "_") { letter | digit | "_" } ;
builtin-type     = "int" | "int8" | "int16" | "int32" | "int64"
                | "float" | "float32" | "float64"
                | "bool" | "None" ;
alias-type       = name ;
struct-type      = name ;
pointer-type     = "ptr" "[" type "]" ;
type            = base-type [ array-suffix ] ;                        (* changed *)
base-type        = builtin-type | alias-type | struct-type | pointer-type ;   (* new *)
array-suffix     = "[" integer "]" ;                                   (* new *)
cast-type        = "int" | "int8" | "int16" | "int32" | "int64"
                | "float" | "float32" | "float64"
                | "bool" | pointer-type ;
integer         = digit { digit } ;
number          = ( digit { digit } [ "." { digit } ]
                  | "." digit { digit } ) [ exponent ] ;
exponent        = ( "e" | "E" ) [ "+" | "-" ] digit { digit } ;
boolean-literal    = "True" | "False" ;
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

`type` now splits into `base-type` plus an optional `array-suffix`, both new productions; `primary` gained `array-literal`, also new. Everything else is unchanged from [Chapter 23](chapter-23.md).

## What's Next

[Chapter 25](chapter-25.md) adds the `class` keyword: a named aggregate type that gets methods, constructors, and visibility rules in the chapters that follow.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.
