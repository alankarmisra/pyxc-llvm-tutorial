---
description: "Add five compound assignment operators and prefix/postfix ++/-- for every assignable expression shape."
---
# 35. pyxc: Read-Modify-Write Operators

## What I Am Building

[Chapter 34](chapter-34.md) let `=` appear inside an expression, not just as a standalone statement. pyxc has had `/` and `%` since [Chapter 4](chapter-04.md) — this chapter isn't about arithmetic operators themselves, it's about convenience over them: I haven't added compound assignment (`+=`, `*=`, etc.), and I haven't added `++` or `--` either. Both are pure sugar over `x = x + 1`-style code; programs don't need them to express anything new. After this chapter, all of that works:

```pyxc
extern def printd(x: float64)

def main() -> int:
  var a: int = 17
  var b: int = 4
  var q: int = a / b
  var r: int = a % b

  var x: int = 10
  x += 5
  x -= 3
  x *= 2
  x /= 4
  x %= 10

  var i: int = 0
  i++
  ++i

  printd(float64(q + r + x + i))
  return 0
```

```
13.000000
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-35
```

## Grammar

I change three areas of the grammar. I replace the bare `=` in `assignment-statement` with `assignment-operator`, now accepting any of the six assignment operators. `term` and `unary-expression` both change to fold in `%` and `++`/`--`, and a new `postfix-expression` production captures postfix `++`/`--` between `unary-expression` and `primary`:

```grammardiff
 program                           = [ end-of-lines ]
                                     [ top-level-item
                                       { end-of-lines top-level-item } ]
                                     [ end-of-lines ] ;
 end-of-lines                      = end-of-line { end-of-line } ;
 top-level-item                    = function-definition
                                     | type-alias
                                     | struct-definition
                                     | external
                                     | top-level-statement ;
 struct-definition                 = "struct" name ":" end-of-lines
                                     struct-block ;
 type-alias                        = "type" name "=" type ;
 struct-block                      = indent field-declaration
                                     { end-of-lines field-declaration } dedent ;
 field-declaration                 = name ":" type ;
 function-definition               = "def" function-signature [ "->" type ] ":"
                                     ( simple-statement
                                       | end-of-lines block ) ;
 external                          = "extern" "def" external-function-signature
                                     [ "->" type ] ;
 top-level-statement               = statement ;
 function-signature                = name "(" [ parameters ] ")" ;
 external-function-signature       = name "(" [ parameters [ "," "..." ] | "..." ] ")" ;
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
 simple-statement                  = return-statement
                                     | break-statement
                                     | continue-statement
                                     | variable-statement
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
 expression                        = assignment ;
-assignment                        = logical-or [ "=" assignment ] ;
+assignment                        = logical-or [ assignment-operator assignment ] ;
 logical-or                        = logical-and { "||" logical-and } ;
 logical-and                       = bitwise-or { "&&" bitwise-or } ;
 bitwise-or                        = bitwise-xor { "|" bitwise-xor } ;
 bitwise-xor                       = bitwise-and { "^" bitwise-and } ;
 bitwise-and                       = equality { "&" equality } ;
 equality                          = relational { ("==" | "!=") relational } ;
 relational                        = shift { ("<" | "<=" | ">" | ">=") shift } ;
 shift                             = sum { ("<<" | ">>") sum } ;
 sum                               = term { ("+" | "-") term } ;
-term                              = factor { ("*" | "/" | "%") factor } ;
+term                              = unary-expression
+                                    { ("*" | "/" | "%") unary-expression } ;
 lvalue                            = name
                                     { "." name | "[" expression "]" } ;
 variable-binding                  = name ":" type [ "=" expression ] ;
-factor                            = ("-" | "!" | "~") factor | primary ;
+unary-expression                  = ("-" | "!" | "~" | "++" | "--")
+                                    unary-expression
+                                    | postfix-expression ;
+postfix-expression                = primary [ "++" | "--" ] ;
 primary                           = cast-expression
                                     | sizeof-expression
                                     | address-expression
                                     | array-literal
                                     | string-literal
                                     | character-literal
                                     | name-expression
                                     | number-expression
                                     | boolean-literal
                                     | parenthesized-expression ;
 cast-expression                   = cast-type "(" expression ")" ;
 sizeof-expression                 = "sizeof" "(" type ")" ;
 address-expression                = "addr" "(" lvalue ")" ;
 array-literal                     = "[" [ expression
                                       { "," expression } ] "]" ;
 string-literal                    = '"' { string-character | escape } '"' ;
 escape                            = literal-escape ;
 string-character                  = ? any character except '"', "\\", "\r", and "\n" ? ;
 character-literal                 = "'" ( character | character-escape ) "'" ;
 character-escape                  = literal-escape ;
 literal-escape                    = "\\" ( "\\" | "'" | '"' | "?"
                                       | "a" | "b" | "f" | "n" | "r"
                                       | "t" | "v"
                                       | "x" hex-digit hex-digit
                                       | octal-digit [ octal-digit
                                         [ octal-digit ] ]
                                       | "u" hex-digit hex-digit hex-digit hex-digit
                                       | "U" hex-digit hex-digit hex-digit hex-digit
                                         hex-digit hex-digit hex-digit hex-digit ) ;
 character                         = ? any character except "'", "\\", "\r", and "\n" ? ;
 hex-digit                         = digit | "A".."F" | "a".."f" ;
+assignment-operator               = "=" | "+=" | "-=" | "*=" | "/=" | "%=" ;
 octal-digit                       = "0".."7" ;
 name-expression                   = lvalue | call-expression ;
 call-expression                   = name "(" [ arguments ] ")" ;
 arguments                         = expression { "," expression } ;
 number-expression                 = number ;
 parenthesized-expression          = "(" expression ")" ;
 indent                            = INDENT ;
 dedent                            = DEDENT ;
 name                              = (letter | "_")
                                     { letter | digit | "_" } ;
 type                              = base-type [ array-suffix ] ;
 base-type                         = builtin-type | alias-type | struct-type
                                     | pointer-type ;
 pointer-type                      = "ptr" "[" type "]" ;
 array-suffix                      = "[" integer "]" ;
 builtin-type                      = "int" | "int8" | "int16" | "int32"
                                     | "int64" | "uint8" | "uint16"
                                     | "uint32" | "uint64"
                                     | "float" | "float32"
                                     | "float64" | "bool" | "None" ;
 struct-type                       = name ;
 alias-type                        = name ;
 cast-type                         = builtin-cast-type | pointer-type ;
 builtin-cast-type                 = "int" | "int8" | "int16" | "int32"
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
 integer                           = digit { digit } ;
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

## New Tokens and Lexer Peek-Ahead

I add seven new tokens to cover the compound assignment operators and the increment/decrement operators:

```cppdiff
 enum Token {
*  ...
*  tok_string = -55,
*  tok_character = -56,
+  tok_plus_equal = -57,
+  tok_minus_equal = -58,
+  tok_star_equal = -59,
+  tok_slash_equal = -60,
+  tok_percent_equal = -61,
+  tok_plus_plus = -62,
+  tok_minus_minus = -63,
*
*  // punctuation and operators
*  tok_lparen = '(',
*  ...
*};
```

I produce each with a one-character peek in the lexer. The `+` path illustrates the pattern: when I see `+`, I peek at the next character to decide between `+=`, `++`, and bare `+`:

```cpp
if (LexerLastChar == '+') {
  int Next = peek();
  int Tok = tok_plus;
  if (Next == '=')
    Tok = (advance(), tok_plus_equal);
  else if (Next == '+')
    Tok = (advance(), tok_plus_plus);
  LexerLastChar = advance();
  return Tok;
}
```

`-` follows the same shape, plus a third alternative for `->` (the return-type arrow, from [Chapter 18](chapter-18.md)):

```cpp
if (LexerLastChar == '-') {
  int Next = peek();
  int Tok = tok_minus;
  if (Next == '>')
    Tok = (advance(), tok_arrow);
  else if (Next == '=')
    Tok = (advance(), tok_minus_equal);
  else if (Next == '-')
    Tok = (advance(), tok_minus_minus);
  LexerLastChar = advance();
  return Tok;
}
```

`*`, `/`, and `%` don't get `++`/`--` forms, so they share one combined branch instead of three separate ones — this is the branch that already returns bare `tok_slash`/`tok_percent` for plain `/` and `%`, since those tokens have existed since Chapter 4. Only the `= ` alternative is new:

```cpp
if (LexerLastChar == '*' || LexerLastChar == '/' ||
    LexerLastChar == '%') {
  int ThisChar = LexerLastChar;
  int Tok = ThisChar == '*' ? tok_star
                            : ThisChar == '/' ? tok_slash : tok_percent;
  if (peek() == '=') {
    advance();
    Tok = ThisChar == '*' ? tok_star_equal
                          : ThisChar == '/' ? tok_slash_equal
                                            : tok_percent_equal;
  }
  LexerLastChar = advance();
  return Tok;
}
```

## One `isLValue()`/`codegenAddress()` Pair, Not Four Node Classes

`p.x`, `arr[i]`, and a plain name are all different `ExpressionNode` subclasses, but they all need to answer the same two questions for this chapter: "can I assign to you?" and "give me your address." Rather than write separate compound-assignment machinery for each lvalue shape, I add two virtual methods to the base `ExpressionNode` that every lvalue-capable node already overrides:

```cpp
virtual bool isLValue() const { return false; }
virtual Value *codegenAddress() { return nullptr; }
```

`NameExpressionNode`, `FieldExpressionNode`, `IndexExpressionNode`, and `IndexedFieldExpressionNode` each override both, returning `true`/a real address; everything else keeps the base class's `false`/`nullptr`. That's the whole trick: one `CompoundAssignmentExpressionNode`, holding a generic `unique_ptr<ExpressionNode> Left`, works for every lvalue shape, because it only ever calls `Left->isLValue()` and `Left->codegenAddress()` — it never needs to know which concrete node `Left` actually is.

```cpp
class CompoundAssignmentExpressionNode : public ExpressionNode {
  unique_ptr<ExpressionNode> Left;
  unique_ptr<ExpressionNode> Right;
  int Operator;

public:
  CompoundAssignmentExpressionNode(unique_ptr<ExpressionNode> Left,
                                   unique_ptr<ExpressionNode> Right,
                                   int Operator, ValueType Type,
                                   const string &StructName = "")
      : Left(std::move(Left)), Right(std::move(Right)), Operator(Operator) {
    setType(Type, StructName);
  }
  bool shouldPrintValue() const override { return false; }
  Value *codegen() override;
};
```

`shouldPrintValue()` returning `false` is what keeps the REPL from auto-printing a `0.0` after every compound assignment — same convention every statement-shaped node has used since [Chapter 11](chapter-11.md).

## Parsing: One `ParseAssignment`, Not Four

`=` and the five compound-assignment operators are all handled by the same function. I parse the left side as an ordinary expression first, then check whether an assignment operator follows — if `Left` isn't an lvalue, that's a parse-time error regardless of which operator comes next:

```cpp
static bool IsAssignmentOperator(int Token) {
  return Token == tok_equal || Token == tok_plus_equal ||
         Token == tok_minus_equal || Token == tok_star_equal ||
         Token == tok_slash_equal || Token == tok_percent_equal;
}

static int AssignmentBinaryOperator(int Token) {
  switch (Token) {
  case tok_plus_equal:
    return tok_plus;
  case tok_minus_equal:
    return tok_minus;
  case tok_star_equal:
    return tok_star;
  case tok_slash_equal:
    return tok_slash;
  case tok_percent_equal:
    return tok_percent;
  default:
    return 0;
  }
}

/// assignment
///   = logical-or [ assignment-operator assignment ] ;
static unique_ptr<ExpressionNode> ParseAssignment() {
  auto Left = ParseLogicalOr();
  if (!Left)
    return nullptr;
  if (!IsAssignmentOperator(CurrentToken))
    return Left;
  if (!Left->isLValue())
    return LogErrorExpression("Assignment target must be assignable");

  ValueType LeftType = Left->getType();
  string LeftTypeInfo = Left->getStructName();
  int AssignmentOperator = CurrentToken;
  getNextToken(); // eat the assignment operator

  ExpectedLiteralTypeGuard Guard(LeftType, LeftTypeInfo);
  auto Right = ParseAssignment();
  if (!Right)
    return nullptr;

  if (LeftType == ValueType::Array)
    return LogErrorExpression("Type mismatch in assignment");

  if (AssignmentOperator != tok_equal) {
    int BinaryOperator = AssignmentBinaryOperator(AssignmentOperator);
    ValueType ResultType =
        GetBinaryResultType(BinaryOperator, LeftType, LeftTypeInfo,
                            Right->getType(), Right->getStructName());
    if (ResultType == ValueType::Error || !IsAssignable(LeftType, ResultType))
      return LogErrorExpression("Type mismatch in assignment");
    if (LeftType == ValueType::Pointer && ResultType != ValueType::Pointer)
      return LogErrorExpression("Type mismatch in assignment");
    return make_unique<CompoundAssignmentExpressionNode>(
        std::move(Left), std::move(Right), BinaryOperator, LeftType,
        LeftTypeInfo);
  }

  // Plain '=' path, unchanged from Chapter 34.
  if (LeftType == ValueType::Pointer &&
      Right->getType() == ValueType::Array) {
    if (!ArrayDecaysToPointerType(Right->getStructName(), LeftTypeInfo))
      return LogErrorExpression("Type mismatch in assignment");
  } else {
    if (!IsAssignable(LeftType, Right->getType()))
      return LogErrorExpression("Type mismatch in assignment");
    if ((LeftType == ValueType::Struct || LeftType == ValueType::Pointer) &&
        LeftTypeInfo != Right->getStructName())
      return LogErrorExpression("Type mismatch in assignment");
  }

  return make_unique<AssignmentExpressionNode>(
      std::move(Left), std::move(Right), LeftType, LeftTypeInfo);
}
```

`Right` recurses back into `ParseAssignment` itself, not one tier down — that's what makes `a = b += 1` parse as `a = (b += 1)`, right-associative, the same shape [Chapter 34](chapter-34.md) already established for plain `=`. The type check reuses `GetBinaryResultType`, the same function every ordinary binary operator's type checking goes through, so `x += y` is rejected under exactly the same rules as `x + y` would be.

## Compound Assignment Codegen

```cpp
static Value *EmitReadModifyWriteValue(int Operator, Value *LeftValue,
                                       ValueType LeftType,
                                       const string &LeftTypeInfo,
                                       Value *RightValue,
                                       ValueType RightType) {
  if (LeftType == ValueType::Pointer) {
    RightValue = EmitImplicitCast(RightValue, RightType, ValueType::Int64);
    if (!RightValue)
      return LogErrorV("Type mismatch in assignment");
    if (Operator == tok_minus)
      RightValue = TheBuilder->CreateNeg(RightValue, "negindex");
    ValueType ElementType = ValueType::Error;
    string ElementTypeInfo;
    if (!DecodePointerType(LeftTypeInfo, ElementType, ElementTypeInfo))
      return LogErrorV("Invalid pointer type metadata");
    return TheBuilder->CreateInBoundsGEP(
        LLVMTypeFor(ElementType, ElementTypeInfo), LeftValue, RightValue,
        "ptrarith");
  }

  RightValue = EmitImplicitCast(RightValue, RightType, LeftType);
  if (!RightValue)
    return LogErrorV("Type mismatch in assignment");
  if (IsFloatType(LeftType)) {
    if (Operator == tok_plus)
      return TheBuilder->CreateFAdd(LeftValue, RightValue, "addtmp");
    if (Operator == tok_minus)
      return TheBuilder->CreateFSub(LeftValue, RightValue, "subtmp");
    if (Operator == tok_star)
      return TheBuilder->CreateFMul(LeftValue, RightValue, "multmp");
    if (Operator == tok_slash)
      return TheBuilder->CreateFDiv(LeftValue, RightValue, "divtmp");
    return TheBuilder->CreateFRem(LeftValue, RightValue, "remtmp");
  }
  if (Operator == tok_plus)
    return TheBuilder->CreateAdd(LeftValue, RightValue, "addtmp");
  if (Operator == tok_minus)
    return TheBuilder->CreateSub(LeftValue, RightValue, "subtmp");
  if (Operator == tok_star)
    return TheBuilder->CreateMul(LeftValue, RightValue, "multmp");
  if (Operator == tok_slash)
    return IsUnsignedIntType(LeftType)
               ? TheBuilder->CreateUDiv(LeftValue, RightValue, "divtmp")
               : TheBuilder->CreateSDiv(LeftValue, RightValue, "divtmp");
  return IsUnsignedIntType(LeftType)
             ? TheBuilder->CreateURem(LeftValue, RightValue, "remtmp")
             : TheBuilder->CreateSRem(LeftValue, RightValue, "remtmp");
}

Value *CompoundAssignmentExpressionNode::codegen() {
  Value *Address = Left->codegenAddress();
  if (!Address)
    return LogErrorV("Assignment target must be assignable");
  Value *LeftValue = TheBuilder->CreateLoad(
      LLVMTypeFor(getType(), getStructName()), Address, "rmw.old");
  Value *RightValue = Right->codegen();
  if (!RightValue)
    return nullptr;
  Value *Result = EmitReadModifyWriteValue(
      Operator, LeftValue, getType(), getStructName(), RightValue,
      Right->getType());
  if (!Result)
    return nullptr;
  TheBuilder->CreateStore(Result, Address);
  return Result;
}
```

`EmitReadModifyWriteValue` is the one place pointer arithmetic (`p += 1`) and ordinary arithmetic (`x += 1`) both go through — the pointer branch is the same `CreateInBoundsGEP` shape [Chapter 26](chapter-26.md) already used for `p + n`, just reached from a different call site. `p.x` never has its own copy of this logic: `Left->codegenAddress()` on a `FieldExpressionNode` already knows how to compute the field's address, so `CompoundAssignmentExpressionNode::codegen()` doesn't need to know or care that `Left` happens to be a field access this time.

## Prefix and Postfix `++`/`--`

One node covers all four combinations of prefix/postfix × increment/decrement:

```cpp
class IncrementDecrementExpressionNode : public ExpressionNode {
  unique_ptr<ExpressionNode> Operand;
  bool IsIncrement;
  bool IsPrefix;

public:
  IncrementDecrementExpressionNode(unique_ptr<ExpressionNode> Operand,
                                   bool IsIncrement, bool IsPrefix,
                                   ValueType Type,
                                   const string &StructName = "")
      : Operand(std::move(Operand)), IsIncrement(IsIncrement),
        IsPrefix(IsPrefix) {
    setType(Type, StructName);
  }
  Value *codegen() override;
};
```

Codegen loads the old value through `Operand->codegenAddress()`, computes `old ± 1` through the same `EmitReadModifyWriteValue` compound assignment already uses, stores the result, and returns `IsPrefix ? NewValue : OldValue`:

```cpp
Value *IncrementDecrementExpressionNode::codegen() {
  Value *Address = Operand->codegenAddress();
  if (!Address)
    return LogErrorV("Increment/decrement target must be assignable");
  Value *OldValue = TheBuilder->CreateLoad(
      LLVMTypeFor(getType(), getStructName()), Address, "incdec.old");
  Value *One = nullptr;
  ValueType OneType = getType();
  if (getType() == ValueType::Pointer) {
    One = ConstantInt::get(Type::getInt64Ty(*TheContext), 1);
    OneType = ValueType::Int64;
  } else if (IsIntType(getType())) {
    One = ConstantInt::get(LLVMTypeFor(getType()), 1);
  } else {
    One = ConstantFP::get(LLVMTypeFor(getType()), 1.0);
  }
  Value *NewValue = EmitReadModifyWriteValue(
      IsIncrement ? tok_plus : tok_minus, OldValue, getType(),
      getStructName(), One, OneType);
  if (!NewValue)
    return nullptr;
  TheBuilder->CreateStore(NewValue, Address);
  return IsPrefix ? NewValue : OldValue;
}
```

Returning `OldValue` for postfix and `NewValue` for prefix is what makes `i++` evaluate to the value before the increment and `++i` evaluate to the value after it, matching C semantics. `p++` on a pointer automatically advances by one element, not one byte, because `EmitReadModifyWriteValue`'s pointer branch is the same code `p += 1` goes through.

## Parsing `++`/`--`

**Postfix** lives in `ParsePostfixExpression`, right after parsing the primary. `unary-expression`'s grammar rule is `("-" | "!" | "~" | "++" | "--") unary-expression | postfix-expression` — postfix is what `ParseUnaryExpression` falls through to once none of the prefix operators match:

```cpp
static unique_ptr<ExpressionNode> ParsePostfixExpression() {
  auto Operand = ParsePrimary();
  if (!Operand)
    return nullptr;
  if (CurrentToken != tok_plus_plus && CurrentToken != tok_minus_minus)
    return Operand;
  if (!Operand->isLValue())
    return LogErrorExpression(
        "Increment/decrement target must be assignable");
  if (!IsNumericType(Operand->getType()) &&
      Operand->getType() != ValueType::Pointer)
    return LogErrorExpression(
        "Increment/decrement requires numeric or pointer type");

  bool IsIncrement = CurrentToken == tok_plus_plus;
  ValueType Type = Operand->getType();
  string TypeInfo = Operand->getStructName();
  getNextToken(); // eat '++' or '--'
  return make_unique<IncrementDecrementExpressionNode>(
      std::move(Operand), IsIncrement, false, Type, TypeInfo);
}
```

**Prefix** is checked first, at the top of `ParseUnaryExpression`, before `-`, `!`, and `~`:

```cpp
static unique_ptr<ExpressionNode> ParseUnaryExpression() {
  if (CurrentToken == tok_plus_plus || CurrentToken == tok_minus_minus) {
    bool IsIncrement = CurrentToken == tok_plus_plus;
    getNextToken(); // eat '++' or '--'
    auto Operand = ParseUnaryExpression();
    if (!Operand)
      return nullptr;
    if (!Operand->isLValue())
      return LogErrorExpression(
          "Increment/decrement target must be assignable");
    if (!IsNumericType(Operand->getType()) &&
        Operand->getType() != ValueType::Pointer)
      return LogErrorExpression(
          "Increment/decrement requires numeric or pointer type");
    ValueType Type = Operand->getType();
    string TypeInfo = Operand->getStructName();
    return make_unique<IncrementDecrementExpressionNode>(
        std::move(Operand), IsIncrement, true, Type, TypeInfo);
  }
  if (CurrentToken == tok_minus)
    return ParseUnaryMinus();
  if (CurrentToken == tok_exclamation) {
    getNextToken(); // eat '!'
    auto Operand = ParseUnaryExpression();
    if (!Operand)
      return nullptr;
    if (Operand->getType() != ValueType::Bool)
      return LogErrorExpression("Unary '!' requires a bool operand");
    return make_unique<UnaryExpressionNode>(tok_exclamation,
                                             std::move(Operand),
                                             ValueType::Bool);
  }
  if (CurrentToken == tok_tilde) {
    getNextToken(); // eat '~'
    auto Operand = ParseUnaryExpression();
    if (!Operand)
      return nullptr;
    if (!IsIntType(Operand->getType()))
      return LogErrorExpression("Unary '~' requires an integer operand");
    ValueType OperandType = Operand->getType();
    return make_unique<UnaryExpressionNode>(tok_tilde, std::move(Operand),
                                             OperandType);
  }
  return ParsePostfixExpression();
}
```

Because `ParseUnaryExpression` recurses on itself for the prefix case, `++--x` is syntactically valid (prefix increment applied to a prefix decrement), though it's only meaningful if `x` is assignable at each level — `isLValue()` is checked fresh at each recursion.

## Build and Run

```bash
cd code/chapter-35
cmake -S . -B build && cmake --build build
./build/pyxc
```

```bash
llvm-lit -v test/
```

## Try It

**Compound assignment on a field**

```pyxc
extern def printd(x: float64)
struct Point:
  x: int
def main() -> int:
  var p: Point
  p.x = 10
  p.x += 5
  printd(float64(p.x))
  return 0
```

```text
15.000000
```

**Compound assignment on an array element**

```pyxc
extern def printd(x: float64)
def main() -> int:
  var arr: int[3] = [1, 2, 3]
  arr[1] *= 10
  printd(float64(arr[1]))
  return 0
```

```text
20.000000
```

**Prefix vs. postfix, as values**

```pyxc
extern def printd(x: float64)
def main() -> int:
  var i: int = 5
  var a: int = i++   # a gets the old value, 5; i becomes 6
  var b: int = ++i   # i becomes 7 first, b gets the new value, 7
  printd(float64(a))
  printd(float64(b))
  printd(float64(i))
  return 0
```

```text
5.000000
7.000000
7.000000
```

## What's Next

[Chapter 36](chapter-36.md) adds the `class` keyword.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.
