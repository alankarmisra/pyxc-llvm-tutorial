---
description: "Add fixed-size stack arrays: declare int[4], initialize with [1, 2, 3, 4], index with arr[i], and pass them anywhere a pointer is expected."
---
# 27. pyxc: Arrays

## What I Am Building

[Chapter 26](chapter-26.md) gave me pointer arithmetic, but the type system still can't express "a fixed number of these, sitting next to each other." For that I need arrays: `int[4]`, allocated on the stack, indexed with `arr[i]`.

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
cd pyxc-llvm-tutorial/code/chapter-27
```

## Grammar

`type` now splits into `base-type` plus an optional `array-suffix`, both new productions; `primary` gains `array-literal`, also new. Everything else is unchanged from [Chapter 26](chapter-26.md):

```grammardiff
 program                           = [ end-of-lines ]
                                     [ top-level-item
                                       { end-of-lines top-level-item } ]
                                     [ end-of-lines ] ;
 end-of-lines                      = end-of-line { end-of-line } ;
 top-level-item                    = function-definition
                                     | struct-definition
                                     | external
                                     | top-level-statement ;
 struct-definition                 = "struct" name ":" end-of-lines
                                     struct-block ;
 struct-block                      = indent field-declaration
                                     { end-of-lines field-declaration } dedent ;
 field-declaration                 = name ":" type ;
 function-definition               = "def" function-signature [ "->" type ] ":"
                                     ( simple-statement
                                       | end-of-lines block ) ;
 external                          = "extern" "def" function-signature [ "->" type ] ;
 top-level-statement               = statement ;
 function-signature                = name "(" [ parameters ] ")" ;
 parameters                        = typed-parameter { "," typed-parameter } ;
 typed-parameter                   = name ":" type ;
 if-statement                      = "if" expression ":" suite
                                     { [ end-of-lines ] "elif" expression ":" suite }
                                     [ [ end-of-lines ] "else" ":" suite ] ;
 for-statement                     = "for" ( "var" name ":" type | name )
                                     "=" expression ","
                                     expression "," expression ":" suite ;
 while-statement                   = "while" expression ":" suite ;
 do-while-statement                = "do" ":" suite [ end-of-lines ]
                                     "while" expression ;
 switch-statement                  = "switch" expression ":" end-of-lines
                                     indent switch-body dedent ;
 switch-body                       = switch-case
                                     { end-of-lines switch-case }
                                     [ end-of-lines default-case ] ;
 switch-case                       = "case" switch-integer
                                     { "," switch-integer } ":" suite ;
 default-case                      = "default" ":" suite ;
 variable-statement                = "var" variable-binding
                                     { "," variable-binding } ;
 assignment-statement              = lvalue "=" expression ;
 simple-statement                  = return-statement
                                     | break-statement
                                     | continue-statement
                                     | variable-statement
                                     | assignment-statement
                                     | expression ;
 compound-statement                = if-statement
                                     | for-statement
                                     | while-statement
                                     | do-while-statement
                                     | switch-statement ;
 statement                         = simple-statement | compound-statement ;
 suite                             = simple-statement
                                     | compound-statement
                                     | end-of-lines block ;
 return-statement                  = "return" [ expression ] ;
 break-statement                   = "break" ;
 continue-statement                = "continue" ;
 statement-separator               = end-of-lines | BLOCK_END ;
 block                             = indent statement
                                     { statement-separator statement } dedent ;
 expression                        = logical-or ;
 logical-or                        = logical-and { "||" logical-and } ;
 logical-and                       = bitwise-or { "&&" bitwise-or } ;
 bitwise-or                        = bitwise-xor { "|" bitwise-xor } ;
 bitwise-xor                       = bitwise-and { "^" bitwise-and } ;
 bitwise-and                       = equality { "&" equality } ;
 equality                          = relational { ("==" | "!=") relational } ;
 relational                        = shift { ("<" | "<=" | ">" | ">=") shift } ;
 shift                             = sum { ("<<" | ">>") sum } ;
 sum                               = term { ("+" | "-") term } ;
 term                              = factor { ("*" | "/" | "%") factor } ;
 lvalue                            = name
                                     { "." name | "[" expression "]" } ;
 variable-binding                  = name ":" type [ "=" expression ] ;
 factor                            = ("-" | "!" | "~") factor | primary ;
 primary                           = cast-expression
                                     | address-expression
+                                    | array-literal
                                     | name-expression
                                     | number-expression
                                     | boolean-literal
                                     | parenthesized-expression ;
 cast-expression                   = cast-type "(" expression ")" ;
 address-expression                = "addr" "(" lvalue ")" ;
+array-literal                     = "[" [ expression
+                                      { "," expression } ] "]" ;
 name-expression                   = lvalue | call-expression ;
 call-expression                   = name "(" [ arguments ] ")" ;
 arguments                         = expression { "," expression } ;
 number-expression                 = number ;
 parenthesized-expression          = "(" expression ")" ;
 indent                            = INDENT ;
 dedent                            = DEDENT ;
 name                              = (letter | "_")
                                     { letter | digit | "_" } ;
-type                              = builtin-type | struct-type | pointer-type ;
+type                              = base-type [ array-suffix ] ;
+base-type                         = builtin-type | struct-type | pointer-type ;
 pointer-type                      = "ptr" "[" type "]" ;
+array-suffix                      = "[" integer "]" ;
 builtin-type                      = "int" | "int8" | "int16" | "int32"
                                     | "int64" | "uint8" | "uint16"
                                     | "uint32" | "uint64"
                                     | "float" | "float32"
                                     | "float64" | "bool" | "None" ;
 struct-type                       = name ;
 cast-type                         = "int" | "int8" | "int16" | "int32"
                                     | "int64" | "uint8" | "uint16"
                                     | "uint32" | "uint64"
                                     | "float" | "float32"
                                     | "float64" | "bool" ;
 number                            = ( digit { digit } [ "." { digit } ]
                                     | "." digit { digit } ) [ exponent ] ;
 switch-integer                    = [ "-" ] digit { digit } ;
 exponent                          = ( "e" | "E" ) [ "+" | "-" ]
                                     digit { digit } ;
 boolean-literal                   = "True" | "False" ;
+integer                           = digit { digit } ;
 letter                            = "A".."Z" | "a".."z" ;
 digit                             = "0".."9" ;
 end-of-line                       = "\r\n" | "\r" | "\n" ;
 (*
     A `comment` begins with "#" and continues to the end of the line. The lexer
      ignores its text and returns an end-of-line token when one follows it.
 *)
 comment                           = "#" { comment-character } ;
 comment-character                 = ? any character except "\r" and "\n" ? ;
 (*
     `whitespace` may appear before or between tokens
      and is ignored by the lexer.
 *)
 whitespace                        = " " | "\t" | "\v" | "\f" ;
 INDENT                            = ? synthetic token emitted by lexer when indentation increases ? ;
 DEDENT                            = ? synthetic token emitted by lexer when indentation decreases ? ;
 BLOCK_END                         = ? synthetic token injected into the stream by ParseBlock
                                       immediately after it consumes DEDENT ? ;

```

## One New Type, One New Node

A single enum value covers every array regardless of element type:

```cppdiff
*enum class ValueType {
*  ...
*  Bool,
*  Struct,
+  Array,
*  Pointer,
*  Error
*};
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
| `float64[3]` | `"12::3"` |
| `Point[2]` | `"14:Point:2"` |

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

The array size itself is read with `std::strtoull`, the same as any other unsigned literal parse in the compiler; there's no dedicated overflow-checked parser for it.

## Extending Type Parsing for Array Suffixes

Before this chapter, `ParseTypeToken` returned the moment it recognized a base type; there was nowhere left to check for a trailing `[4]`. I have it collect the base type into a local (`BaseType`/`BaseTypeInfo`) instead of returning immediately, so I can look at what follows before deciding what to return:

```cpp
if (CurrentToken == tok_lbracket) {
  if (BaseType == ValueType::None) {
    LogErrorExpression("Arrays of None are not allowed");
    return ValueType::Error;
  }
  getNextToken(); // eat '['
  if (CurrentToken != tok_number || NumberIsFloat) {
    LogErrorExpression("Array size must be an integer literal");
    return ValueType::Error;
  }
  uint64_t ElementCount = std::strtoull(NumberLiteral.c_str(), nullptr, 10);
  if (ElementCount == 0) {
    LogErrorExpression("Array size must be greater than zero");
    return ValueType::Error;
  }
  getNextToken(); // eat array size
  if (CurrentToken != tok_rbracket) {
    LogErrorExpression("Expected ']' after array size");
    return ValueType::Error;
  }
  getNextToken(); // eat ']'
  if (CurrentToken == tok_lbracket) {
    LogErrorExpression("Nested arrays are not supported");
    return ValueType::Error;
  }
  BaseTypeInfo = EncodeArrayType(BaseType, BaseTypeInfo, ElementCount);
  BaseType = ValueType::Array;
}

if (StructName)
  *StructName = BaseTypeInfo;
return BaseType;
```

The only nested-array case this rejects is a literal double suffix like `int[4][2]`: the check right after the closing `]` catches a second `[` immediately following. pyxc has no type-alias mechanism yet at this chapter, so there's no other route to a hidden array-of-array. I verified the rejection, and separately that a non-literal size is rejected too:

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
  string SavedTypeInfo;
  ExpectedLiteralTypeGuard(ValueType Type, const string &TypeInfo = "")
      : Saved(ExpectedLiteralType), SavedTypeInfo(ExpectedLiteralTypeInfo) {
    ExpectedLiteralType = Type;
    ExpectedLiteralTypeInfo = TypeInfo;
  }
  ~ExpectedLiteralTypeGuard() {
    ExpectedLiteralType = Saved;
    ExpectedLiteralTypeInfo = SavedTypeInfo;
  }
};
```

`ReturnTypeGuard` gets the identical treatment, for the same reason: a function returning `int[4]` needs that full context available while its body parses.

With that in place, parsing the literal itself is straightforward: read the expected element type and count out of the guard, then parse exactly that many elements:

```cpp
static unique_ptr<ExpressionNode> ParseArrayLiteralExpression() {
  if (ExpectedLiteralType != ValueType::Array)
    return LogErrorExpression("Array literal requires an expected array type");

  ValueType ElementType = ValueType::Error;
  string ElementStructName;
  uint64_t ExpectedCount = 0;
  if (!DecodeArrayType(ExpectedLiteralTypeInfo, ElementType,
                       ElementStructName, ExpectedCount))
    return LogErrorExpression("Invalid expected array type");

  getNextToken(); // eat '['
  vector<unique_ptr<ExpressionNode>> Elements;
  if (CurrentToken != tok_rbracket) {
    while (true) {
      ExpectedLiteralTypeGuard Guard(ElementType, ElementStructName);
      auto Element = ParseExpression();
      if (!Element)
        return nullptr;
      if (!IsAssignable(ElementType, Element->getType()) ||
          ((ElementType == ValueType::Struct ||
            ElementType == ValueType::Pointer) &&
           ElementStructName != Element->getStructName()))
        return LogErrorExpression("Array literal element type mismatch");
      Elements.push_back(std::move(Element));
      if (CurrentToken != tok_comma)
        break;
      getNextToken(); // eat ','
    }
  }
  if (CurrentToken != tok_rbracket)
    return LogErrorExpression("Expected ']' after array literal");
  getNextToken(); // eat ']'
  if (Elements.size() != ExpectedCount)
    return LogErrorExpression("Array literal element count mismatch");
  return make_unique<ArrayLiteralExpressionNode>(
      std::move(Elements), ExpectedLiteralTypeInfo);
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
  ValueType ElementType = ValueType::Error;
  string ElementStructName;
  uint64_t ElementCount = 0;
  if (!DecodeArrayType(getStructName(), ElementType, ElementStructName,
                       ElementCount))
    return LogErrorValue("Invalid array literal type");

  Value *Aggregate = UndefValue::get(LLVMTypeFor(getType(), getStructName()));
  for (size_t Index = 0; Index < Elements.size(); ++Index) {
    Value *Element = Elements[Index]->codegen();
    if (!Element)
      return nullptr;
    Element = EmitImplicitCast(Element, Elements[Index]->getType(), ElementType);
    if (!Element)
      return LogErrorValue("Array literal element type mismatch");
    Aggregate = TheBuilder->CreateInsertValue(Aggregate, Element,
                                           {static_cast<unsigned>(Index)},
                                           "arrayinit");
  }
  return Aggregate;
}
```

The element-count check (`Elements.size() != Count`) doesn't need to be repeated here: `ParseArrayLiteralExpression` already rejected a mismatched count before this node was ever constructed, so by the time codegen runs the counts are guaranteed to agree.

Whatever consumes this value, a `var` initializer, is what actually stores it into stack memory; this function never allocates anything itself.

## Indexing: One Address Node, Two Kinds of Base

`arr[i]` and `p[i]` both parse into the same `IndexExpressionNode` from chapter 26 (`Base`, an `Index`, plus the element type), because `ParseNameExpressionWithName`'s chaining loop only checks that the current result is a `Pointer` before wrapping it. An array variable's type is `Array`, not `Pointer`, at parse time, so I widen that check to also accept `Array` and decode the element type from `DecodeArrayType` instead of `DecodePointerType` when the base is one.

The real branching happens in codegen, where `IndexExpressionNode::codegenAddress()` decides how to reach the base address depending on whether the base is a pointer or an array:

```cpp
Value *IndexExpressionNode::codegenAddress() {
  Value *IndexValue = Index->codegen();
  if (!IndexValue)
    return nullptr;
  IndexValue = TheBuilder->CreateIntCast(
      IndexValue, Type::getInt64Ty(*TheContext),
      !IsUnsignedIntType(Index->getType()), "index");

  if (Base->getType() == ValueType::Array) {
    Value *ArrayAddress = Base->codegenAddress();
    if (!ArrayAddress)
      return nullptr;
    Value *Zero = ConstantInt::get(Type::getInt64Ty(*TheContext), 0);
    return TheBuilder->CreateInBoundsGEP(
        LLVMTypeFor(Base->getType(), Base->getStructName()), ArrayAddress,
        {Zero, IndexValue}, "elemptr");
  }

  Value *BasePointer = Base->codegen();
  if (!BasePointer)
    return nullptr;
  return TheBuilder->CreateInBoundsGEP(
      LLVMTypeFor(getType(), getStructName()), BasePointer, IndexValue,
      "elemptr");
}
```

For a pointer, `Base->codegen()` loads whatever address it holds, and a single-index GEP steps by `IndexValue` elements from there. For an array, there's no pointer stored anywhere to load: the array's storage *is* the value, so I take `Base->codegenAddress()` (the alloca or global itself) and GEP through it with two indices, a zero to step into the array and `IndexValue` to reach the element, the same shape LLVM expects for indexing directly into an aggregate. I compiled `scores[2]` and read the IR rather than assuming, and it's a single `getelementptr` with two indices, not two chained GEPs:

```llvm
%elemptr = getelementptr inbounds [4 x i64], ptr %scores, i64 0, i64 2
%elemload = load i64, ptr %elemptr, align 8
```

## Using an Array Where a Pointer Is Expected

Indexing isn't the only place an array needs to become a pointer. Passing an array variable as a whole, to a function expecting `ptr[T]`, needs the same decay. `NameExpressionNode::codegen` checks for an `Array` type before it does its normal load, and GEPs to the first element instead:

```cppdiff
*Value *NameExpressionNode::codegen() {
+  if (getType() == ValueType::Array) {
+    Value *ArrayAddress = codegenAddress();
+    if (!ArrayAddress)
+      return LogErrorValue("Unknown variable name");
+    Value *Zero = ConstantInt::get(Type::getInt64Ty(*TheContext), 0);
+    return TheBuilder->CreateInBoundsGEP(
+        LLVMTypeFor(getType(), getStructName()), ArrayAddress, {Zero, Zero},
+        "arraydecay");
+  }
+
*  auto It = NamedValues.find(Name);
*  if (It != NamedValues.end() && It->second)
*    return TheBuilder->CreateLoad(LLVMTypeFor(getType(), getStructName()), It->second,
*                               Name.c_str());
*
*  if (auto *GV = GetGlobalVariable(Name))
*    return TheBuilder->CreateLoad(LLVMTypeFor(getType(), getStructName()), GV,
*                               Name.c_str());
*
*  return LogErrorValue("Unknown variable name");
*}
```

Loading an array by name would otherwise hand back the entire aggregate, which is only meaningful as something to `store`, not something to pass around as a value. Function-call argument checking uses `ArrayDecaysToPointerType` to allow this specific case through even though `ptr[int]` and `int[4]` aren't the same `ValueType`. It's a separate branch ahead of the normal `IsAssignable` check, since `IsAssignable` still has no idea an array and a pointer can ever mix, and `continue`s past the ordinary struct/pointer-name comparison below it once it's satisfied:

```cpp
for (size_t i = 0; i < Arguments.size(); ++i) {
  ValueType ArgType = Arguments[i]->getType();
  ValueType ParamType = Signature->getParameterType(i);
  if (ParamType == ValueType::Pointer && ArgType == ValueType::Array) {
    if (!ArrayDecaysToPointerType(
            Arguments[i]->getStructName(),
            Signature->getParameterStructName(i)))
      return LogErrorExpression("Argument type mismatch");
    continue;
  }
  if (!IsAssignable(ParamType, ArgType)) {
    return LogErrorExpression(("argument " + std::to_string(i + 1) + " expects " +
                     TypeName(ParamType))
                        .c_str());
  }
  ...
}
```

By the time the argument reaches codegen, `NameExpressionNode::codegen` has already turned it into a pointer-typed LLVM value through the `arraydecay` GEP shown above, so no further cast is needed there; but the same array-to-pointer decay is also legal in a `var` initializer and a plain assignment (`var p: ptr[int] = scores`, `p = scores`), which route the value through `EmitImplicitCast` instead. I add the same permissive case there, right at the top before any of the existing numeric or float casting rules:

```cppdiff
*static Value *EmitImplicitCast(Value *V, ValueType From, ValueType To) {
*  if (From == To)
*    return V;
+  if (From == ValueType::Array && To == ValueType::Pointer)
+    return V;
*  if (IsFloatType(From) && IsFloatType(To)) {
*    ...
*  }
*  ...
*}
```

It returns `V` unchanged for the same reason: whatever produced `V` (a `NameExpressionNode` reference to an array) has already done the GEP that turns the aggregate into an element pointer, so by the time `EmitImplicitCast` sees it, it's already the right kind of LLVM value; the function just has to stop rejecting the combination instead of doing any actual conversion work.

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
cd code/chapter-27
cmake -S . -B build && cmake --build build
```

```bash
llvm-lit -v test/
```

## Try It

### Declare, Initialize, Index

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

### An Array Parameter, Indexed in the Body

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

### An Array Decaying to a Pointer Argument

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

### Element Count Mismatch

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

**No heap arrays.** Everything in this chapter lives on the stack. For dynamically sized or long-lived data, [Chapter 28](chapter-28.md)'s `malloc` and `ptr[T]` are still what I reach for.

**Struct fields can't be arrays yet.** Struct fields are still limited to scalar, pointer, and struct types.

**No pointer arithmetic directly on an array.** Indexing works; adding an integer to an array variable itself doesn't. `addr(arr[i])` gets a pointer to a specific element if I need one.

## What's Next

[Chapter 28](chapter-28.md) adds heap allocation with `malloc`/`free`/`sizeof`.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.
