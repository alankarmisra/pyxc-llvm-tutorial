---
description: "Add &&, ||, and ! — logical operators with short-circuit evaluation and a dedicated bool result type."
---
# 33. pyxc: Logical Operators

## What I Am Building

[Chapter 32](chapter-32.md) completed arithmetic: division, remainder, compound assignment, and `++`/`--`. Conditions in `if` can already involve comparisons, but there's still no way to combine two boolean checks or negate one. After this chapter:

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
cd pyxc-llvm-tutorial/code/chapter-33
```

## Grammar

`&&` and `||` don't join an existing operator list — pyxc's grammar since [Chapter 17](chapter-17.md) is a fixed-tier recursive-descent grammar, not a generic precedence table, so each new operator with its own precedence level needs its own tier. `logical-or` and `logical-and` become the two new outermost tiers, sitting above `comparison`. `!` joins `unary-expression`'s existing alternation:

```grammardiff
 program         = [ end-of-lines ] [ top-level-item { end-of-lines top-level-item } ] [ end-of-lines ] ;
 end-of-lines            = end-of-line { end-of-line } ;
 top-level-item             = type-alias | trait-definition | struct-definition | class-definition | implementation-definition | function-definition | external | top-level-expression ;
 type-alias       = "type" name "=" type ;
 trait-definition        = "trait" name [ "[" name "]" ] ":" end-of-lines trait-block ;
 trait-block      = indent trait-method-signature { end-of-lines trait-method-signature } dedent ;
 trait-method-signature  = "def" name "(" [ typed-parameter { "," typed-parameter } ] ")" [ "->" type ] ;
 struct-definition       = "struct" name ":" end-of-lines struct-block ;
 class-definition        = "class" name [ "(" trait-reference { "," trait-reference } ")" ] ":" end-of-lines struct-block ;
 trait-reference        = name [ "[" type "]" ] ;
 implementation-definition         = "impl" trait-reference "for" name ":" end-of-lines implementation-block ;
 implementation-block       = indent implementation-method { end-of-lines implementation-method } dedent ;
 implementation-method      = "def" name "(" [ typed-parameter { "," typed-parameter } ] ")" [ "->" type ] ":" ( simple-statement | end-of-lines block ) ;
 struct-block     = indent class-member { end-of-lines class-member } dedent ;
 class-member     = [ visibility ] ( field-declaration | method-definition ) ;
 visibility      = "public" | "private" ;
 method-definition       = "def" name "(" [ typed-parameter { "," typed-parameter } ] ")"
                   [ "->" type ] ":" ( simple-statement | end-of-lines block ) ;
 field-declaration       = name ":" type ;
 function-definition      = "def" function-signature [ "->" type ] ":" ( simple-statement | end-of-lines block ) ;
 (* If the return type is omitted, it defaults to None. *)
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
 assignment-statement      = lvalue assignment-operator expression ; (* assignment is a statement here *)
 simple-statement      = return-statement | variable-statement | assignment-statement | expression ;
 compound-statement    = if-statement | for-statement ;
 statement       = simple-statement | compound-statement ;
 suite           = simple-statement | compound-statement | end-of-lines block ;
 return-statement      = "return" [ expression ] ;
 statement-separator = end-of-lines | BLOCK_END ;
 block = indent statement { statement-separator statement } dedent ;
-expression      = comparison ;
+expression      = logical-or ;
+logical-or      = logical-and { "||" logical-and } ;
+logical-and     = comparison { "&&" comparison } ;
 comparison      = sum { comparison-operator sum } ;
 comparison-operator = "==" | "!=" | "<=" | ">=" | "<" | ">" ;
 sum             = term { ("+" | "-") term } ;
 term            = unary-expression { ("*" | "/" | "%") unary-expression } ;
 lvalue          = name | field-access | index-expression ;
 variable-binding      = name ":" type [ "=" expression ] ;
-unary-expression       = ("-" | "++" | "--") unary-expression | postfix-expression ;
+unary-expression       = ("-" | "!" | "++" | "--") unary-expression | postfix-expression ;
 postfix-expression     = primary [ postfix-operator ] ;
 postfix-operator       = "++" | "--" ;
 primary         = cast-expression | sizeof-expression | address-expression | array-literal | string-literal | name-expression | field-access | index-expression | number-expression | boolean-literal | parenthesized-expression ;
 cast-expression        = cast-type "(" expression ")" ;
 sizeof-expression      = "sizeof" "(" type ")" ;
 address-expression        = "addr" "(" lvalue ")" ;
 name-expression  = name | call-expression | method-call-expression | constructor-call-expression ;
 call-expression        = name "(" [ expression { "," expression } ] ")" ;
 method-call-expression  = name "." name "(" [ expression { "," expression } ] ")" ;
 constructor-call-expression    = name "(" [ expression { "," expression } ] ")" ;
 field-access     = name "." name { "." name } ;
 index-expression       = name "[" expression "]" ;
 number-expression      = number ;
 array-literal    = "[" [ expression { "," expression } ] "]" ;
 string-literal   = "\"" { ? any char except " and newline ? | escape } "\"" ;
 escape          = "\\" ( "\\" | "\"" | "n" | "t" | "0" ) ;
 parenthesized-expression       = "(" expression ")" ;
 indent          = INDENT ;
 dedent          = DEDENT ;
 
 assignment-operator        = "=" | "+=" | "-=" | "*=" | "/=" | "%=" ;
 name      = (letter | "_") { letter | digit | "_" } ;
 builtin-type     = "int" | "int8" | "int16" | "int32" | "int64"
                 | "float" | "float32" | "float64"
                 | "bool" | "None" ;
 alias-type       = name ;
 struct-type      = name ;
 pointer-type     = "ptr" "[" type "]" ;
 type            = base-type [ array-suffix ] ;
 base-type        = builtin-type | alias-type | struct-type | pointer-type ;
 array-suffix     = "[" integer "]" ;
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

Putting `logical-and` below `logical-or` but above `comparison` is what makes `&&` bind tighter than `||`: `a || b && c` parses as `a || (b && c)`, since `logical-or` only ever sees whole `logical-and` results as its operands.

## New Tokens and Lexer Peek-Ahead

Two new token values:

```cpp
tok_and = -50, // &&
tok_or  = -51, // ||
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

`ParseLogicalAnd` and `ParseLogicalOr` follow the exact same shape every other binary-operator tier has used since [Chapter 20](chapter-20.md): a base case that parses one level down, and a `*Right` helper that consumes a run of same-precedence operators, both funneling through `MergeBinaryExpression`:

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

`ParseExpression` becomes a one-line call to `ParseLogicalOr`, the new outermost tier. `MergeBinaryExpression` is the same helper [Chapter 20](chapter-20.md) introduced for pointer arithmetic; it's what calls `GetBinaryResultType` for type checking and builds the `BinaryExpressionNode`, so `&&`/`||` don't need any parsing machinery of their own beyond these two tiers.

## Type-Checking `&&` and `||`

A predicate identifies the two logical operators:

```cpp
static bool IsLogicalOp(int Operator) { return Operator == tok_and || Operator == tok_or; }
```

`GetBinaryResultType` gains a branch for them: both operands must already be `bool`, with no implicit conversion from anything else:

```cpp
if (IsLogicalOp(Operator)) {
  if (L == ValueType::Bool && R == ValueType::Bool)
    return ValueType::Bool;
  return ValueType::Error;
}
```

An `int` on either side of `&&` fails here and reports the same generic "Type mismatch in binary operator" every other operator mismatch reports; there's nothing `&&`/`||`-specific about the error message.

## Built-In `!` for Bool

`!` gets its own AST node:

```cpp
class LogicalNotExpressionNode : public ExpressionNode {
  unique_ptr<ExpressionNode> Operand;

public:
  explicit LogicalNotExpressionNode(unique_ptr<ExpressionNode> Operand)
      : Operand(std::move(Operand)) {
    setType(ValueType::Bool);
  }
  Value *codegen() override;
};
```

The constructor sets the result type to `Bool` immediately; there's nothing to infer. Parsing happens in `ParseUnary`, right alongside unary minus and prefix `++`/`--`:

```cpp
if (CurrentToken == tok_exclamation) {
  getNextToken(); // eat '!'
  auto Operand = ParseUnary();
  if (!Operand)
    return nullptr;
  if (Operand->getType() != ValueType::Bool)
    return LogErrorExpression("Unary '!' requires a bool operand");
  return make_unique<LogicalNotExpressionNode>(std::move(Operand));
}
```

`!` only ever accepts a `bool` operand; anything else is a parse-time error right there, not a fallback to some other mechanism. Codegen is a single `CreateNot` on the `i1` value:

```cpp
Value *LogicalNotExpressionNode::codegen() {
  Value *V = Operand->codegen();
  if (!V)
    return nullptr;
  if (Operand->getType() != ValueType::Bool)
    return LogErrorV("Type mismatch in unary operator");
  return Builder->CreateNot(V, "nottmp");
}
```

## Short-Circuit Codegen for `&&` and `||`

`&&` and `||` skip the ordinary binary-operator codegen path entirely. At the top of `BinaryExpressionNode::codegen`, they're intercepted before the right operand is ever evaluated:

```cpp
Value *BinaryExpressionNode::codegen() {
  if (Operator == tok_and || Operator == tok_or) {
    Value *L = Left->codegen();
    if (!L)
      return nullptr;
    if (Left->getType() != ValueType::Bool || Right->getType() != ValueType::Bool)
      return LogErrorV("Type mismatch in binary operator");

    Function *F = Builder->GetInsertBlock()->getParent();
    BasicBlock *LHSBB = Builder->GetInsertBlock();
    BasicBlock *RHSBB = BasicBlock::Create(*TheContext, "logic.rhs", F);
    BasicBlock *MergeBB = BasicBlock::Create(*TheContext, "logic.end");

    if (Operator == tok_and)
      Builder->CreateCondBr(L, RHSBB, MergeBB);
    else
      Builder->CreateCondBr(L, MergeBB, RHSBB);

    Builder->SetInsertPoint(RHSBB);
    Value *RHSVal = Right->codegen();
    if (!RHSVal)
      return nullptr;
    if (Right->getType() != ValueType::Bool)
      return LogErrorV("Type mismatch in binary operator");
    Builder->CreateBr(MergeBB);
    RHSBB = Builder->GetInsertBlock();

    F->insert(F->end(), MergeBB);
    Builder->SetInsertPoint(MergeBB);
    PHINode *PN =
        Builder->CreatePHI(Type::getInt1Ty(*TheContext), 2, "logictmp");
    if (Operator == tok_and) {
      PN->addIncoming(ConstantInt::getFalse(*TheContext), LHSBB);
      PN->addIncoming(RHSVal, RHSBB);
    } else {
      PN->addIncoming(ConstantInt::getTrue(*TheContext), LHSBB);
      PN->addIncoming(RHSVal, RHSBB);
    }
    return PN;
  }
  // ...ordinary binary-operator path for everything else...
}
```

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
```

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
cd code/chapter-33
cmake -S . -B build && cmake --build build
```

## What's Next

[Chapter 34](chapter-34.md) adds `while`, `do`/`while`, `break`, and `continue`.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.
