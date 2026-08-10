---
description: "Add Python-style elif chains so multi-way conditionals don't nest into a pyramid of else blocks."
---
# 37. pyxc: `elif` Chains

## What I Am Building

In [Chapter 12](chapter-12.md), I added an `if` statement without an `elif`. For multiple conditions, I'm forced to write multiple nested if statements:

```pyxc
def classify(x: int) -> int:
  if x < 0:
    return -1
  else:
    if x == 0:
      return 0
    else:
      return 1
```

This isn't great. After this chapter, I can write the same logic as:

```pyxc
def classify(x: int) -> int:
  if x < 0:
    return -1
  elif x == 0:
    return 0
  else:
    return 1
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-37
```

## Grammar

I add one alternative to `if-statement`: zero or more `elif` clauses between the `if` clause and the optional `else`:

`code/chapter-37/pyxc.ebnf`

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
- if-statement          = "if" expression ":" suite
-                [ end-of-lines "else" ":" suite ] ;
+ if-statement          = "if" expression ":" suite
+                { end-of-lines "elif" expression ":" suite }
+                [ end-of-lines "else" ":" suite ] ;
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
 expression      = logical-or ;
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
 switch-integer       = [ "-" ] integer ;
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

## New Token and Keyword

I add one new token:

```cpp
tok_elif = -63,
```

I add it to the keyword table:

```cpp
{"elif", tok_elif},
```

And I add it to the token name map for error messages:

```cpp
{tok_elif, "'elif'"},
```

## Refactoring If/Elif Parsing to Collect Branches

Previously, I parsed a single condition and body in `ParseIfStatement`. Now I collect an arbitrary number of `(condition, body)` pairs in a loop before I know whether an `else` follows:

```cpp
static unique_ptr<ExpressionNode> ParseIfStatement() {
  getNextToken(); // eat 'if'
  vector<pair<unique_ptr<ExpressionNode>, unique_ptr<ExpressionNode>>> Branches;
  bool LastBranchWasBlock = false;

  while (true) {
    auto Cond = ParseExpression();
    if (!Cond)
      return nullptr;
    if (Cond->getType() != ValueType::Bool)
      return LogErrorExpression("If condition must be bool");

    if (CurrentToken != tok_colon)
      return LogErrorExpression("Expected ':' after if/elif condition");
    getNextToken(); // eat ':'

    auto Body = ParseSuite();
    if (!Body)
      return nullptr;
    LastBranchWasBlock = (CurrentToken == tok_block_end);
    if (LastBranchWasBlock)
      getNextToken();
    Branches.push_back({std::move(Cond), std::move(Body)});

    consumeNewlines();
    if (CurrentToken != tok_elif)
      break;
    getNextToken(); // eat 'elif'
  }
  // ...
}
```

After each body, I call `consumeNewlines()` to skip the line ending. If the next token is `tok_elif`, I continue the loop — eating `elif` and parsing another condition and body. On any other token, including `tok_else`, I break out of the loop.

I run every condition, `if` or `elif`, through the same `Cond->getType() != ValueType::Bool` check. I don't need a separate rule for `elif`; it's the same branch of the same loop.

**Missing colon after an `elif` condition:**
```pyxc
elif x == 0
    return 0
```
```
Error (Line 4, Column 14): Expected ':' after if/elif condition
  elif x == 0
             ^~~~
```

**Non-bool `elif` condition:**
```pyxc
elif x + 1:
    return 0
```
```
Error (Line 4, Column 13): If condition must be bool
  elif x + 1:
            ^~~~
```

## Lowering to a Nested If Tree

I don't introduce a new AST node for this. Once the loop above exits, I check for a trailing `else`:

```cpp
unique_ptr<ExpressionNode> Else;
if (CurrentToken == tok_else) {
  getNextToken(); // eat 'else'
  if (CurrentToken != tok_colon)
    return LogErrorExpression("Expected ':' after else");
  getNextToken(); // eat ':'
  Else = ParseSuite();
  if (!Else)
    return nullptr;
} else if (LastBranchWasBlock) {
  // No else: restore the synthetic separator for the enclosing block/top level.
  PendingTokens.push_front(CurrentToken);
  CurrentToken = tok_block_end;
}
```

If there's no `else`, I leave `Else` null — but if the last branch's body ended a block (consuming a `tok_block_end`), that separator was meant for whatever encloses this `if`, not for the `if` itself, so I push the current token back and re-inject `tok_block_end` so the enclosing block still sees its separator.

I then lower the `elif` chain directly to a right-nested `IfStatementNode` tree: the (possibly null) `else` body becomes the initial innermost node, and I walk `Branches` in reverse:

```cpp
// Lower if/elif chain to nested IfStatementNode in else branch.
unique_ptr<ExpressionNode> Tree = std::move(Else);
for (auto It = Branches.rbegin(); It != Branches.rend(); ++It) {
  Tree = make_unique<IfStatementNode>(std::move(It->first), std::move(It->second),
                                std::move(Tree));
}
return Tree;
```

Given:
```pyxc
if a:    body_a
elif b:  body_b
elif c:  body_c
else:    body_d
```

I build:

```ast
IfStatementNode(a, body_a,
  IfStatementNode(b, body_b,
    IfStatementNode(c, body_c,
      body_d)))
```

If there's no `else` at all, I leave the innermost node's `Else` null, and my `IfStatementNode` codegen already treats a null `Else` as "fall through" — the same thing a bare `if` without `else` has always done. I don't change anything about that path for `elif`.

My codegen sees exactly what it would see for hand-written nested `if`/`else` blocks, so at `-O0` the IR evaluates conditions top to bottom, same as nested `if`/`else` would. At higher optimization levels LLVM may turn the chain into a `switch` or lookup table on its own. I still reach for `switch` from Chapter 36 when dispatching on compile-time integer constants; I use `elif` for everything else.

## Build and Run

```bash
cd code/chapter-37
cmake -S . -B build && cmake --build build
```

## Try It

```pyxc
ready> def classify(x: int) -> int:
   if x < 0:
     return -1
   elif x == 0:
     return 0
   else:
     return 1

ready> classify(-5)
-1
ready> classify(0)
0
ready> classify(9)
1
ready>
```

## What's Next

[Chapter 38](chapter-38.md) adds character literals: `'a'`, `'\n'`, `'\t'`, and friends.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.
