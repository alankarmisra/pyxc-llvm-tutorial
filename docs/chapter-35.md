---
description: "Complete pyxc's arithmetic: add / and %, five compound assignment operators, and prefix/postfix ++/-- for all lvalue shapes."
---
# 35. pyxc: Arithmetic Completeness

## What I Am Building

In [Chapter 42](chapter-42.md), I finished the object model. Before moving further, I want to close a gap: I've given pyxc `+`, `-`, and `*`, but not `/` or `%`. I haven't added compound assignment (`+=`, `*=`, etc.), and I haven't added `++` or `--` either. After this chapter, all of that works:

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
cd pyxc-llvm-tutorial/code/chapter-32
```

## Grammar

I change three areas of the grammar. I replace the bare `=` in `assignment-statement` with `assignment-operator`, now accepting any of the six assignment operators. `term` and `unary-expression` both change to fold in `%` and `++`/`--`, and a new `postfix-expression` production captures postfix `++`/`--` between `unary-expression` and `primary`:

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
-assignment-statement      = lvalue "=" expression ; (* assignment is a statement here *)
+assignment-statement      = lvalue assignment-operator expression ; (* assignment is a statement here *)
 simple-statement      = return-statement | variable-statement | assignment-statement | expression ;
 compound-statement    = if-statement | for-statement ;
 statement       = simple-statement | compound-statement ;
 suite           = simple-statement | compound-statement | end-of-lines block ;
 return-statement      = "return" [ expression ] ;
 statement-separator = end-of-lines | BLOCK_END ;
 block = indent statement { statement-separator statement } dedent ;
 expression      = comparison ;
 comparison      = sum { comparison-operator sum } ;
 comparison-operator = "==" | "!=" | "<=" | ">=" | "<" | ">" ;
 sum             = term { ("+" | "-") term } ;
-term            = unary-expression { ("*" | "/") unary-expression } ;
+term            = unary-expression { ("*" | "/" | "%") unary-expression } ;
 lvalue          = name | field-access | index-expression ;
 variable-binding      = name ":" type [ "=" expression ] ;
-unary-expression       = "-" unary-expression | primary ;
+unary-expression       = ("-" | "++" | "--") unary-expression | postfix-expression ;
+postfix-expression     = primary [ postfix-operator ] ;
+postfix-operator       = "++" | "--" ;
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
 
+assignment-operator        = "=" | "+=" | "-=" | "*=" | "/=" | "%=" ;
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

## New Tokens and Lexer Peek-Ahead

I add seven new tokens to cover the compound assignment operators and the increment/decrement operators:

```cpp
tok_pluseq     = -45,   // +=
tok_minuseq    = -46,   // -=
tok_muleq      = -47,   // *=
tok_diveq      = -48,   // /=
tok_modeq      = -49,   // %=
tok_plusplus   = -56,   // ++
tok_minusminus = -57,   // --
```

I produce each with a one-character peek in the lexer. The `+` path illustrates the pattern: when I see `+`, I peek at the next character to decide between `+=`, `++`, and bare `+`:

```cpp
if (LexerLastChar == '+') {
  int Next = peek();
  int Tok = tok_plus;
  if (Next == '=')
    Tok = (advance(), tok_pluseq);
  else if (Next == '+')
    Tok = (advance(), tok_plusplus);
  LexerLastChar = advance();
  return Tok;
}
```

I apply the same pattern to `-` (which must also handle `->` for the arrow token), `*`, `/`, and `%`. The `/` path is new — previously `/` was an unknown character. Now I return `'/'` bare, or `tok_diveq` if it's followed by `=`.

## Division and Remainder

I add `/` and `%` to the precedence table at level 40 — the same level as `*`:

```cpp
{tok_slash, 40},   // /
{tok_percent, 40}, // %
```

The LLVM instructions I emit from `EmitBuiltInArithmetic` differ by type:

| Op | Integer | Float |
|----|---------|-------|
| `/` | `sdiv` | `fdiv` |
| `%` | `srem` | error |

`%` on float operands is a type error — I return `ValueType::Error` from `GetBinaryResultType` when either operand of `%` is not an integer:

```cpp
if (Operator == tok_percent && (!IsIntType(L) || !IsIntType(R)))
  return ValueType::Error;
```

I report the resulting `ValueType::Error` as a type mismatch — the same generic error every binary operator falls back to, not something specific to `%`:

```pyxc
def main() -> int:
  var a: float64 = 5.5
  var b: float64 = 2.0
  var r: float64 = a % b
  return 0
```
```
Error (Line 4, Column 25): Type mismatch in binary operator
  var r: float64 = a % b
                        ^~~~
```

I also tighten the pointer arithmetic guard: only `+` and `−` allow a pointer on one side. I now explicitly reject `/` and `%` with a pointer operand:

```cpp
if ((Operator == tok_plus || Operator == tok_minus) &&
    ((L == ValueType::Pointer && IsIntType(R)) ||
     (R == ValueType::Pointer && IsIntType(L)))) {
  // pointer arithmetic
}
```

## Compound Assignment AST Nodes

I add four AST node classes, one for each lvalue shape, all sharing the same structure: an lvalue, an operator token, and an RHS expression:

```cpp
class CompoundAssignmentExpressionNode : public ExpressionNode {  // plain variable
  string Name; int Operator; unique_ptr<ExpressionNode> Right; ...
};
class FieldCompoundAssignmentExpressionNode : public ExpressionNode {  // p.x += 1
  unique_ptr<FieldExpressionNode> Left; int Operator; unique_ptr<ExpressionNode> Right; ...
};
class IndexCompoundAssignmentExpressionNode : public ExpressionNode {  // arr[i] *= 2
  unique_ptr<IndexExpressionNode> Left; int Operator; unique_ptr<ExpressionNode> Right; ...
};
class IndexedFieldCompoundAssignmentExpressionNode : public ExpressionNode {  // arr[i].x += 3
  unique_ptr<IndexedFieldExpressionNode> Left; int Operator; unique_ptr<ExpressionNode> Right; ...
};
```

I make all four override `shouldPrintValue()` to return `false` — compound assignment is a statement, not a value expression, so the REPL doesn't auto-print its result.

I drive the parse dispatch with two helpers: I check whether the current token is one of the five compound assignment tokens (`IsCompoundAssignTok`), then convert it to the corresponding arithmetic operator character (`CompoundAssignToBinaryOp`) so codegen can call `EmitBuiltInArithmetic`:

```cpp
static bool IsCompoundAssignTok(int Tok) {
  return Tok == tok_pluseq || Tok == tok_minuseq || Tok == tok_muleq ||
         Tok == tok_diveq  || Tok == tok_modeq;
}
static int CompoundAssignToBinaryOp(int Tok) {
  switch (Tok) {
  case tok_pluseq:
    return tok_plus;
  case tok_minuseq:
    return tok_minus;
  case tok_muleq:
    return tok_star;
  case tok_diveq:
    return tok_slash;
  case tok_modeq:
    return tok_percent;
  default:
    return 0;
  }
}
```

I handle the plain-variable case in `ParseCompoundAssignmentRight`: I look up the destination type, convert the token to a binary op, call `ParseExpression` for the right-hand side, type-check the result, and return a `CompoundAssignmentExpressionNode`. The field case follows the same pattern in its own helper, `ParseFieldCompoundAssignmentRight`. The index and indexed-field cases follow the identical pattern too, but inline inside `ParseLeadingNameSimpleStatement` rather than in their own helpers.

I write codegen the same way for all four nodes: resolve the lvalue to a pointer, load the current value, call `EmitBuiltInArithmetic(Operator, old, right)`, and store the result back.

## Prefix and Postfix `++`/`--`

I handle all four combinations of prefix/postfix × increment/decrement with a single AST node:

```cpp
class IncDecExpressionNode : public ExpressionNode {
  unique_ptr<ExpressionNode> Operand;
  bool IsIncrement;
  bool IsPrefix;

public:
  IncDecExpressionNode(unique_ptr<ExpressionNode> Operand, bool IsIncrement, bool IsPrefix,
                ValueType Type, const string &StructName = "")
      : Operand(std::move(Operand)), IsIncrement(IsIncrement),
        IsPrefix(IsPrefix) {
    setType(Type, StructName);
  }
  Value *codegen() override;
};
```

I require the operand to pass `IsIncDecAssignableExpr` — it must be a variable, field, index, or indexed-field expression:

```cpp
static bool IsIncDecAssignableExpr(const ExpressionNode *E) {
  return dynamic_cast<const NameExpressionNode *>(E) ||
         dynamic_cast<const FieldExpressionNode *>(E) ||
         dynamic_cast<const IndexExpressionNode *>(E) ||
         dynamic_cast<const IndexedFieldExpressionNode *>(E);
}
```

In codegen, I load the old value, compute `old ± 1` via `EmitBuiltInArithmetic`, store the new value, and return `IsPrefix ? new : old`. For postfix, I return the value that existed *before* the mutation, matching C semantics.

Because I reuse `EmitBuiltInArithmetic` here — the same function `+` and `-` already go through — `p++` on a pointer automatically advances by one element through the pointer-arithmetic path from earlier in this chapter. I don't need to add anything pointer-specific here.

## Parsing `++`/`--`

**Postfix** I handle in `ParsePostfixIncDec`, which wraps the primary expression in an `IncDecExpressionNode` if it's followed by `++` or `--`. Both paths require the operand to be numeric or a pointer, in addition to being assignable. `ParseUnary` calls `ParsePostfixIncDec(ParsePrimary())` instead of calling `ParsePrimary` alone:

```cpp
static unique_ptr<ExpressionNode> ParsePostfixIncDec(unique_ptr<ExpressionNode> Base) {
  while (CurrentToken == tok_plusplus || CurrentToken == tok_minusminus) {
    bool IsIncrement = (CurrentToken == tok_plusplus);
    if (!IsIncDecAssignableExpr(Base.get()))
      return LogErrorExpression("Increment/decrement target must be assignable");
    if (!IsNumericType(Base->getType()) &&
        Base->getType() != ValueType::Pointer)
      return LogErrorExpression("Increment/decrement requires numeric or pointer type");
    ValueType T = Base->getType();
    string S = Base->getStructName();
    getNextToken(); // eat ++/--
    Base = make_unique<IncDecExpressionNode>(std::move(Base), IsIncrement,
                                      /*IsPrefix=*/false, T, S);
  }
  return Base;
}
```

**Prefix** I handle at the top of `ParseUnary`, before the primary:

```cpp
if (CurrentToken == tok_plusplus || CurrentToken == tok_minusminus) {
  bool IsIncrement = (CurrentToken == tok_plusplus);
  getNextToken(); // eat ++/--
  auto Operand = ParseUnary();
  if (!Operand)
    return nullptr;
  if (!IsIncDecAssignableExpr(Operand.get()))
    return LogErrorExpression("Increment/decrement target must be assignable");
  if (!IsNumericType(Operand->getType()) &&
      Operand->getType() != ValueType::Pointer)
    return LogErrorExpression("Increment/decrement requires numeric or pointer type");
  return make_unique<IncDecExpressionNode>(std::move(Operand), IsIncrement,
                                    /*IsPrefix=*/true, Operand->getType(),
                                    Operand->getStructName());
}
```

Because `ParseUnary` recurses, `++++x` is syntactically valid (prefix applied twice), though I only accept it as meaningful if `x` is assignable at each level.

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
