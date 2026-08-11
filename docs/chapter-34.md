---
section: "Expression and Mutation Conveniences"
description: "Allow assignment inside an expression so patterns like while (c = getchar()) != EOF work without a separate priming read."
---
# 34. pyxc: Assignment as Expression

## What I Am Building

[Chapter 19](chapter-19.md) added unsigned integer types. pyxc can call `getchar()`, but the canonical K&R idiom for reading until EOF still doesn't compile:

```pyxc
# What I want to write:
while (c = getchar()) != EOF:
    ...
```
```
Error (Line 5, Column 12): expected ')'
  while (c = 
           ^~~~
```

The problem is that `=` is a statement in pyxc — it can't appear inside an expression like a `while` condition. After this chapter it can:

```pyxc
extern def getchar() -> int32
extern def printd(x: float64)

var EOF: int32 = -1

def main() -> int:
  var c: int32
  var blanks: int
  while (c = getchar()) != EOF:
    if c == ' ':
      blanks += 1
  printd(float64(blanks))
  return 0
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-41
```

## Grammar

I rename `expression` to `assignment` and make it optionally recurse on itself after an assignment operator. Every `expression` in the grammar now means "possibly an assignment":

`code/chapter-41/pyxc.ebnf`

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
                 { end-of-lines "elif" expression ":" suite }
                 [ end-of-lines "else" ":" suite ] ;
 while-statement       = "while" expression ":" suite ;
 do-while-statement     = "do" ":" suite end-of-lines "while" expression ;
 switch-statement      = "switch" expression ":" end-of-lines indent switch-body dedent ;
 switch-body      = switch-case { end-of-lines switch-case } [ end-of-lines default-case ] ;
 switch-case      = "case" switch-integer { "," switch-integer } ":" suite ;
 default-case     = "default" ":" suite ;
 for-statement         = "for"
                   ( "var" name ":" type | name )
                   "=" expression "," expression "," expression ":" suite ;
 variable-statement         = "var" variable-binding { "," variable-binding } ;
 assignment-statement      = lvalue assignment-operator expression ; (* assignment is a statement here *)
 simple-statement      = return-statement | break-statement | continue-statement | variable-statement | assignment-statement | expression ;
 compound-statement    = if-statement | for-statement | while-statement | do-while-statement | switch-statement ;
 statement       = simple-statement | compound-statement ;
 suite           = simple-statement | compound-statement | end-of-lines block ;
 return-statement      = "return" [ expression ] ;
 break-statement       = "break" ;
 continue-statement    = "continue" ;
 statement-separator = end-of-lines | BLOCK_END ;
 block = indent statement { statement-separator statement } dedent ;
-expression      = logical-or ;
+expression      = assignment ;
+assignment      = logical-or [ assignment-operator assignment ] ;
 logical-or      = logical-and { "||" logical-and } ;
 logical-and     = bitwise-or { "&&" bitwise-or } ;
 bitwise-or      = bitwise-xor { "|" bitwise-xor } ;
 bitwise-xor     = bitwise-and { "^" bitwise-and } ;
 bitwise-and     = equality { "&" equality } ;
 equality        = relational { ("==" | "!=") relational } ;
 relational      = shift { ("<" | "<=" | ">" | ">=") shift } ;
 shift           = sum { ("<<" | ">>") sum } ;
 sum             = term { ("+" | "-") term } ;
 term            = unary-expression { ("*" | "/" | "%") unary-expression } ;
 lvalue          = name | field-access | index-expression ;
 variable-binding      = name ":" type [ "=" expression ] ;
 unary-expression       = ("-" | "!" | "~" | "++" | "--") unary-expression | postfix-expression ;
 postfix-expression     = primary [ postfix-operator ] ;
 postfix-operator       = "++" | "--" ;
 primary         = cast-expression | sizeof-expression | address-expression | array-literal | string-literal | character-literal | name-expression | field-access | index-expression | number-expression | boolean-literal | parenthesized-expression ;
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
 string-literal   = "\"" { ? valid Unicode scalar value except " and newline, encoded as UTF-8 ? | literal-escape } "\"" ;
 character-literal     = "'" ( ? valid Unicode scalar value except ' and newline, encoded as UTF-8 ? | literal-escape ) "'" ;
 literal-escape   = "\\" ( simple-escape | octal-escape | "x" hex-digit hex-digit
                    | "u" hex-digit hex-digit hex-digit hex-digit
                    | "U" hex-digit hex-digit hex-digit hex-digit
                          hex-digit hex-digit hex-digit hex-digit ) ;
 simple-escape    = "a" | "b" | "f" | "n" | "r" | "t" | "v"
                  | "\\" | "'" | "\"" | "?" ;
 octal-escape     = octal-digit [ octal-digit [ octal-digit ] ] ;
 parenthesized-expression       = "(" expression ")" ;
 indent          = INDENT ;
 dedent          = DEDENT ;
 
 assignment-operator        = "=" | "+=" | "-=" | "*=" | "/=" | "%=" ;
 name      = (letter | "_") { letter | digit | "_" } ;
 builtin-type     = "int" | "int8" | "int16" | "int32" | "int64"
                 | "uint8" | "uint16" | "uint32" | "uint64"
                 | "float" | "float32" | "float64"
                 | "bool" | "None" ;
 alias-type       = name ;
 struct-type      = name ;
 pointer-type     = "ptr" "[" type "]" ;
 type            = base-type [ array-suffix ] ;
 base-type        = builtin-type | alias-type | struct-type | pointer-type ;
 array-suffix     = "[" integer "]" ;
 cast-type        = "int" | "int8" | "int16" | "int32" | "int64"
                 | "uint8" | "uint16" | "uint32" | "uint64"
                 | "float" | "float32" | "float64"
                 | "bool" | pointer-type ;
 integer         = digit { digit } ;
 switch-integer       = [ "-" ] integer ;
 number          = ( digit { digit } [ "." { digit } ]
                   | "." digit { digit } ) [ exponent ] ;
 exponent        = ( "e" | "E" ) [ "+" | "-" ] digit { digit } ;
 boolean-literal    = "True" | "False" ;
 letter          = "A".."Z" | "a".."z" ;
 digit           = "0".."9" ;
 hex-digit       = digit | "A".."F" | "a".."f" ;
 octal-digit     = "0".."7" ;
 end-of-line             = "\r\n" | "\r" | "\n" ;
 comment = "#" { comment-character } ;
 comment-character = ? any character except "\r" and "\n" ? ;
 whitespace = " " | "\t" | "\v" | "\f" ;
 INDENT          = ? synthetic token emitted by lexer ? ;
 DEDENT          = ? synthetic token emitted by lexer ? ;
 
 BLOCK_END = ? synthetic token injected into the stream by ParseBlock immediately after it consumes DEDENT ? ;
```

Assignment sits at the loosest level, below every operator tier. `assignment-statement` (assignment as its own statement, one per line) is unchanged — it still works exactly as before.

## Tail Assignment Check

`ParseExpression` already parses one full `logical-or` — the top of the operator-precedence chain from Chapter 22. I add a check after it returns: if an assignment operator follows, treat what I just parsed as an lvalue and parse the right-hand side by recursing into `ParseExpression` again:

```cpp
static unique_ptr<ExpressionNode> ParseExpression() {
  auto Left = ParseLogicalOr();
  if (!Left)
    return nullptr;

  if (CurrentToken != tok_equal && !IsCompoundAssignTok(CurrentToken))
    return Left;

  int AssignTok = CurrentToken;
  getNextToken(); // eat assignment operator
  ExpectedLiteralTypeGuard Guard(Left->getType(), Left->getStructName());
  auto Right = ParseExpression(); // right-associative assignment
  if (!Right)
    return nullptr;
  return BuildAssignmentExpr(AssignTok, std::move(Left), std::move(Right));
}
```

`ExpectedLiteralTypeGuard` propagates the lvalue's type into the right-hand parse, so `x = 5` when `x` is `int32` treats `5` as `int32` without needing an explicit cast.

Since `Left` is a full `logical-or` — everything up through comparisons — before I ever check for `=`, a bare `c = getchar() != EOF` doesn't do what it looks like it does:

```pyxc
while c = getchar() != EOF:
```
```
Error (Line 5, Column 29): Type mismatch in assignment
  while c = getchar() != EOF:
                            ^~~~
```

`c` gets parsed first, then everything after `=` — including the `!=` comparison — recurses as the right-hand side. So this parses as `c = (getchar() != EOF)`: I'm assigning a `bool` to an `int32`, hence the type mismatch. `while (c = getchar()) != EOF:` needs the parentheses to force `c = getchar()` to bind first.

## Lvalue Validation and Node Construction

All lvalue validation happens in `BuildAssignmentExpr`, a new helper extracted from the old statement-level assignment parser. It pattern-matches on the node type of `Left`:

```cpp
static unique_ptr<ExpressionNode> BuildAssignmentExpr(int AssignTok,
                                               unique_ptr<ExpressionNode> Left,
                                               unique_ptr<ExpressionNode> Right) {
  if (!Left || !Right)
    return nullptr;

  if (auto *Var = dynamic_cast<NameExpressionNode *>(Left.get())) {
    // plain variable: produce AssignmentExpressionNode or CompoundAssignmentExpressionNode
    ...
  }
  if (auto *Field = dynamic_cast<FieldExpressionNode *>(Left.get())) {
    // field access: produce FieldAssignmentExpressionNode or its compound variant
    ...
  }
  if (auto *Idx = dynamic_cast<IndexExpressionNode *>(Left.get())) {
    // array index: produce IndexAssignmentExpressionNode or its compound variant
    ...
  }
  if (auto *IdxField = dynamic_cast<IndexedFieldExpressionNode *>(Left.get())) {
    // indexed field: produce IndexedFieldAssignmentExpressionNode or its compound variant
    ...
  }

  return LogErrorExpression("Assignment target must be assignable");
}
```

I recognize four lvalue kinds: variable, field, array index, and indexed field. Everything else — `x + 1`, a function call, anything that isn't one of those four node types — falls through to the final `LogErrorExpression`:

```pyxc
(x + 1) = 3
```
```
Error (Line 3, Column 14): Assignment target must be assignable
  (x + 1) = 3
             ^~~~
```

The type-checking inside each branch (`IsAssignable`, struct-name matching) isn't new — I reuse the existing `AssignmentExpressionNode`, `CompoundAssignmentExpressionNode`, and their field/index variants unchanged. `BuildAssignmentExpr` is the only new code; it just decides which one to build.

## The Value of an Assignment Expression

`AssignmentExpressionNode::codegen` already stored the assigned value and returned it — that part didn't need to change. What's new is that this return value is now reachable from inside a larger expression, since `ParseExpression` can produce an assignment node at any nesting level:

```pyxc
var result: int = (c = 5) + 1   # result is 6; c is 5
```

Compound assignment works the same way: `(n -= 1)` produces the new value of `n`, so `while (n -= 1) > 0:` works too.

I don't print this value at the top level, though — a bare `c = 5` typed at the REPL still sets `c` silently, exactly like before this chapter.

## Right Associativity

The recursive call is to `ParseExpression`, not back into the tighter precedence tiers — so `=` chains right:

```pyxc
a = b = 4   # parsed as: a = (b = 4)
```

`b = 4` evaluates first: it stores 4 into `b` and produces 4. Then `a = 4` stores that into `a`. Both end up 4.

## Parentheses Are Transparent for Lvalues

There's no `ParenExpressionNode` in pyxc. `ParseParenthesizedExpression` just parses the inner expression and returns it directly:

```cpp
static unique_ptr<ExpressionNode> ParseParenthesizedExpression() {
  getNextToken(); // eat (.
  auto V = ParseExpression();
  if (!V)
    return nullptr;

  if (CurrentToken != tok_rparen)
    return LogErrorExpression("expected ')'");
  getNextToken(); // eat ).
  return V;
}
```

So `(x)` and `x` produce the identical AST node — parentheses never wrap anything, they just group. That means `BuildAssignmentExpr` never even sees that parens were there; assignment through them works for free:

```pyxc
(x) = 2     # valid: same as x = 2
(p[i]) = v  # valid: same as p[i] = v
```

## Try It

```pyxc
ready> var a: int = 0
ready> var b: int = 0
ready> a = b = 4
ready> a
4
ready> b
4
ready> var n: int = 5
ready> (n -= 1)
ready> n
4
ready>
```

## What's Next

[Chapter 35](chapter-35.md) adds compound assignment and `++`/`--`.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.
