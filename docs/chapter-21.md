---
description: "Add &&, ||, and ! — logical operators with short-circuit evaluation and a dedicated bool result type."
---
# 21. pyxc: Logical Operators

## What I Am Building

[Chapter 20](chapter-20.md) added debug info and the real optimization pipeline. Conditions in `if` can already involve comparisons, but there's still no way to combine two boolean checks or negate one. After this chapter:

```pyxc
extern def printd(x: float64)

def is_between(x: int, lo: int, hi: int) -> bool:
  return x >= lo && x <= hi

def main() -> int:
  var a: bool = True
  var b: bool = !a
  if b || is_between(5, 1, 10):
    printd(1.0)
  return 0
```

```text
1.000000
```

`&&` and `||` short-circuit: the right-hand side isn't evaluated if the result is already determined by the left. I confirmed this isn't just documentation-talk by giving the right-hand side a real side effect (a `printd` call) and checking it genuinely never runs when the left side already decides the outcome.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-21
```

## Grammar

`&&` and `||` don't join an existing operator list — pyxc's grammar since [Chapter 18](chapter-18.md) is a fixed-tier recursive-descent grammar, not a generic precedence table, so each new operator with its own precedence level needs its own tier. `logical-or` and `logical-and` become the two new outermost tiers, sitting above `comparison`. `!` joins `factor`'s existing alternation:

```grammardiff
 program                           = [ end-of-lines ]
                                     [ top-level-item
                                       { end-of-lines top-level-item } ]
                                     [ end-of-lines ] ;
 end-of-lines                      = end-of-line { end-of-line } ;
 top-level-item                    = function-definition
                                     | external
                                     | top-level-statement ;
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
                                     | do-while-statement ;
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
-expression                        = comparison ;
+expression                        = logical-or ;
+logical-or                        = logical-and { "||" logical-and } ;
+logical-and                       = comparison { "&&" comparison } ;
 comparison                        = sum { comparison-operator sum } ;
 comparison-operator               = "==" | "!=" | "<=" | ">=" | "<" | ">" ;
 sum                               = term { ("+" | "-") term } ;
 term                              = factor { ("*" | "/" | "%") factor } ;
 lvalue                            = name ;
 variable-binding                  = name ":" type [ "=" expression ] ;
-factor                            = "-" factor | primary ;
+factor                            = ("-" | "!") factor | primary ;
 primary                           = cast-expression
                                     | name-expression
                                     | number-expression
                                     | boolean-literal
                                     | parenthesized-expression ;
 cast-expression                   = cast-type "(" expression ")" ;
 name-expression                   = name | call-expression ;
 call-expression                   = name "(" [ arguments ] ")" ;
 arguments                         = expression { "," expression } ;
 number-expression                 = number ;
 parenthesized-expression          = "(" expression ")" ;
 indent                            = INDENT ;
 dedent                            = DEDENT ;
 name                              = (letter | "_")
                                     { letter | digit | "_" } ;
 type                              = "int" | "int8" | "int16" | "int32"
                                     | "int64" | "uint8" | "uint16"
                                     | "uint32" | "uint64"
                                     | "float" | "float32"
                                     | "float64" | "bool" | "None" ;
 cast-type                         = "int" | "int8" | "int16" | "int32"
                                     | "int64" | "uint8" | "uint16"
                                     | "uint32" | "uint64"
                                     | "float" | "float32"
                                     | "float64" | "bool" ;
 number                            = ( digit { digit } [ "." { digit } ]
                                     | "." digit { digit } ) [ exponent ] ;
 exponent                          = ( "e" | "E" ) [ "+" | "-" ]
                                     digit { digit } ;
 boolean-literal                   = "True" | "False" ;
 letter                            = "A".."Z" | "a".."z" ;
 digit                             = "0".."9" ;
 end-of-line                       = "\r\n" | "\r" | "\n" ;
 comment                           = "#" { comment-character } ;
 comment-character                 = ? any character except "\r" and "\n" ? ;
 whitespace                        = " " | "\t" | "\v" | "\f" ;
 INDENT                            = ? synthetic token emitted by lexer when indentation increases ? ;
 DEDENT                            = ? synthetic token emitted by lexer when indentation decreases ? ;
 BLOCK_END                         = ? synthetic token injected into the stream by ParseBlock
                                       immediately after it consumes DEDENT ? ;
 
 BLOCK_END = ? synthetic token injected into the stream by ParseBlock immediately after it consumes DEDENT ? ;
```

Putting `logical-and` below `logical-or` but above `comparison` is what makes `&&` bind tighter than `||`: `a || b && c` parses as `a || (b && c)`, since `logical-or` only ever sees whole `logical-and` results as its operands.

## New Tokens and Lexer Peek-Ahead

Two new token values:

```cppdiff
*enum Token {
*  ...
*  tok_uint64 = -42,
+  tok_and = -43, // &&
+  tok_or = -44,  // ||
*
*  // punctuation and operators
*  tok_lparen = '(',
*  ...
*};
```

Single `&` and `|` remain their own ASCII-valued tokens; they're bitwise operators, added in a later chapter, and distinct from `&&`/`||`. The lexer peeks one character ahead to decide which to emit:

```cpp
if (LexerLastChar == '&') {
  int Tok = (peek() == '&') ? (advance(), tok_and) : '&';
  LexerLastChar = advance();
  return Tok;
}

if (LexerLastChar == '|') {
  int Tok = (peek() == '|') ? (advance(), tok_or) : '|';
  LexerLastChar = advance();
  return Tok;
}
```

If the next character is another `&` or `|`, `advance()` consumes it and the two-character token is returned. Otherwise the single-character token falls through unchanged.

## Parsing `&&` and `||` as Their Own Grammar Tiers

`ParseLogicalAnd` and `ParseLogicalOr` follow the exact same shape every other binary-operator tier has used since [Chapter 18](chapter-18.md): a base case that parses one level down, and a `*Right` helper that consumes a run of same-precedence operators, both funneling through `MergeBinaryExpression`:

```cpp
static unique_ptr<ExpressionNode>
ParseLogicalAndRight(unique_ptr<ExpressionNode> Left) {
  while (CurrentToken == tok_and) {
    int Operator = CurrentToken;
    getNextToken();
    auto Right = ParseComparison();
    if (!Right)
      return nullptr;
    Left = MergeBinaryExpression(Operator, std::move(Left), std::move(Right));
    if (!Left)
      return nullptr;
  }
  return Left;
}

static unique_ptr<ExpressionNode> ParseLogicalAnd() {
  auto Left = ParseComparison();
  if (!Left)
    return nullptr;
  return ParseLogicalAndRight(std::move(Left));
}

static unique_ptr<ExpressionNode>
ParseLogicalOrRight(unique_ptr<ExpressionNode> Left) {
  while (CurrentToken == tok_or) {
    int Operator = CurrentToken;
    getNextToken();
    auto Right = ParseLogicalAnd();
    if (!Right)
      return nullptr;
    Left = MergeBinaryExpression(Operator, std::move(Left), std::move(Right));
    if (!Left)
      return nullptr;
  }
  return Left;
}

static unique_ptr<ExpressionNode> ParseLogicalOr() {
  auto Left = ParseLogicalAnd();
  if (!Left)
    return nullptr;
  return ParseLogicalOrRight(std::move(Left));
}
```

`ParseExpression` becomes a one-line call to `ParseLogicalOr`, the new outermost tier. `MergeBinaryExpression` is the same helper [Chapter 18](chapter-18.md) introduced for typed binary operators generally; it's what calls `GetBinaryResultType` for type checking and builds the `BinaryExpressionNode`, so `&&`/`||` don't need any parsing machinery of their own beyond these two tiers.

## Type-Checking `&&` and `||`

A predicate identifies the two logical operators:

```cpp
static bool IsLogicalOp(int Operator) {
  return Operator == tok_and || Operator == tok_or;
}
```

`GetBinaryResultType` gains a branch for them: both operands must already be `bool`, with no implicit conversion from anything else:

```cppdiff
*static ValueType GetBinaryResultType(int Operator, ValueType L, ValueType R) {
*  if (IsArithmeticOp(Operator)) {
*    ...
*  }
*  if (IsComparisonOp(Operator)) {
*    ...
*  }
+  if (IsLogicalOp(Operator)) {
+    if (L == ValueType::Bool && R == ValueType::Bool)
+      return ValueType::Bool;
+    return ValueType::Error;
+  }
*  return ValueType::Error;
*}
```

An `int` on either side of `&&` fails here and reports the same generic "Type mismatch in binary operator" every other operator mismatch reports; there's nothing `&&`/`||`-specific about the error message.

## Built-in `!` for Bool

`!` doesn't get a dedicated AST node — it reuses [Chapter 10](chapter-10.md)'s `UnaryExpressionNode`, the same class unary minus already uses, tagged with its own opcode and result type:

```cpp
class UnaryExpressionNode : public ExpressionNode {
  char Opcode;
  unique_ptr<ExpressionNode> Operand;

public:
  UnaryExpressionNode(char Opcode, unique_ptr<ExpressionNode> Operand, ValueType Type)
      : Opcode(Opcode), Operand(std::move(Operand)) {
    setType(Type);
  }
  Value *codegen() override;
};
```

Parsing happens in `ParseFactor`, right alongside unary minus — `factor`'s grammar rule already reads `("-" | "!") factor | primary`:

```cpp
static unique_ptr<ExpressionNode> ParseFactor() {
  if (CurrentToken == tok_minus)
    return ParseUnaryMinus();
  if (CurrentToken == tok_exclamation) {
    getNextToken(); // eat '!'
    auto Operand = ParseFactor();
    if (!Operand)
      return nullptr;
    if (Operand->getType() != ValueType::Bool)
      return LogErrorExpression("Unary '!' requires a bool operand");
    return make_unique<UnaryExpressionNode>(tok_exclamation,
                                             std::move(Operand),
                                             ValueType::Bool);
  }
  return ParsePrimary();
}
```

`!` only ever accepts a `bool` operand; anything else is a parse-time error right there, not a fallback to some other mechanism. `UnaryExpressionNode::codegen()` branches on `Opcode`, so `!` just adds one more case next to unary minus's existing `CreateNeg`/`CreateFNeg` branch:

```cpp
Value *UnaryExpressionNode::codegen() {
  Value *Operator = Operand->codegen();
  if (!Operator)
    return nullptr;

  // Built-in unary minus.
  if (Opcode == tok_minus) {
    if (IsIntType(getType()))
      return TheBuilder->CreateNeg(Operator, "negtmp");
    if (IsFloatType(getType()))
      return TheBuilder->CreateFNeg(Operator, "negtmp");
    return LogErrorV("Unary '-' not supported for this type");
  }

  if (Opcode == tok_exclamation)
    return TheBuilder->CreateNot(Operator, "nottmp");

  return LogErrorV("Unknown unary operator");
}
```

## Short-Circuit Codegen for `&&` and `||`

`&&` and `||` skip the ordinary binary-operator codegen path entirely. At the top of `BinaryExpressionNode::codegen`, they're intercepted before the right operand is ever evaluated:

```cppdiff
*Value *BinaryExpressionNode::codegen() {
+  if (Operator == tok_and || Operator == tok_or) {
+    Value *LeftValue = Left->codegen();
+    if (!LeftValue)
+      return nullptr;
+
+    Function *FunctionIR = TheBuilder->GetInsertBlock()->getParent();
+    BasicBlock *LeftBlock = TheBuilder->GetInsertBlock();
+    BasicBlock *RightBlock =
+        BasicBlock::Create(*TheContext, "logic.rhs", FunctionIR);
+    BasicBlock *MergeBlock = BasicBlock::Create(*TheContext, "logic.end");
+
+    if (Operator == tok_and)
+      TheBuilder->CreateCondBr(LeftValue, RightBlock, MergeBlock);
+    else
+      TheBuilder->CreateCondBr(LeftValue, MergeBlock, RightBlock);
+
+    TheBuilder->SetInsertPoint(RightBlock);
+    Value *RightValue = Right->codegen();
+    if (!RightValue)
+      return nullptr;
+    TheBuilder->CreateBr(MergeBlock);
+    RightBlock = TheBuilder->GetInsertBlock();
+
+    FunctionIR->insert(FunctionIR->end(), MergeBlock);
+    TheBuilder->SetInsertPoint(MergeBlock);
+    PHINode *Result =
+        TheBuilder->CreatePHI(Type::getInt1Ty(*TheContext), 2, "logictmp");
+    if (Operator == tok_and) {
+      Result->addIncoming(ConstantInt::getFalse(*TheContext), LeftBlock);
+      Result->addIncoming(RightValue, RightBlock);
+    } else {
+      Result->addIncoming(ConstantInt::getTrue(*TheContext), LeftBlock);
+      Result->addIncoming(RightValue, RightBlock);
+    }
+    return Result;
+  }
+
*  Value *L = Left->codegen();
*  ...
*}
```

There's no runtime type check here — `Left->getType() != ValueType::Bool` never gets asked at codegen time, because it's already been settled at parse time. `MergeBinaryExpression` calls `GetBinaryResultType` before it ever builds this `BinaryExpressionNode`, and that's where a non-`bool` operand to `&&`/`||` gets rejected — codegen only ever runs on a tree that already type-checked.

For `a && b`: evaluate `a`. If false, branch straight to `logic.end` carrying a `false` constant. If true, fall into `logic.rhs`, evaluate `b`, then branch to `logic.end`. The `phi` node in `logic.end` picks between the `false` constant (the short-circuit path) and whatever `b` actually evaluated to.

For `a || b` the branch condition is inverted: if `a` is true, jump straight to `logic.end` carrying a `true` constant; otherwise fall into evaluating `b`. The `phi` node picks between `true` and `b`'s result the same way.

I confirmed the short-circuit is real, not just documentation, by giving `b` an observable side effect (a `printd` call) and checking it genuinely never runs when `a` alone already decides the result — see Try It.

## Known Limitations

**`&&` and `||` are strictly boolean, not bitwise.** There's no implicit `int`-to-`bool` conversion on either side, and no bitwise AND/OR on integers yet.

## Try It

**Non-bool operand is a type error**

```pyxc
def main() -> int:
  var x: int = 1
  var y: bool = True
  if x && y:
    return 1
  return 0
```

```text
Error (Line 4, Column 12): Type mismatch in binary operator
  if x && y:
           ^~~~
Error (Line 5, Column 5): Unexpected indentation
    r
    ^~~~
Error (Line 6, Column 11): cannot return a value from a None function
  return 0
          ^~~~
```

The first line is the real error; the rest is the parser recovering from the malformed statement, the same cascading behavior earlier chapters' error examples already showed.

**The right-hand side genuinely doesn't run**

```pyxc
extern def printd(x: float64)

def sideeffect() -> bool:
  printd(99.0)
  return True

def main() -> int:
  var f: bool = False
  if f && sideeffect():
    printd(1.0)
  printd(2.0)
  return 0
```

```text
2.000000
```

`sideeffect()` is never called: `f` is `False`, so `f && sideeffect()` never falls into `logic.rhs` at all, and `99.000000` never prints.

## Build and Run

```bash
cd code/chapter-21
cmake -S . -B build && cmake --build build
```

```bash
llvm-lit -v test/
```

## What's Next

[Chapter 22](chapter-22.md) adds bitwise operators.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.
