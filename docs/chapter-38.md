---
description: "Add character literals so single characters can be written as 'a', '\n', '\t', and '\\' instead of their numeric ASCII values."
---
# 38. pyxc: Character Literals

## Where We Are

Since I don't support character literals in pyxc just yet, I'm forced to write code like so:

```pyxc
if c == 32:   # space
if c == 10:   # newline — or was it 13?
```

But I want to write it like a sane person would:

```pyxc
if c == ' ':
if c == '\n':
```

I'll introduce character literals into pyxc. 

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-38
```

## Grammar

I add `character-literal` as a `primary` alternative, and two new productions for its content:

`code/chapter-38/pyxc.ebnf`

```grammardiff
 program         = [ end-of-lines ] [ top-level-item { end-of-lines top-level-item } ] [ end-of-lines ] ;
 end-of-lines            = end-of-line { end-of-line } ;
 top-level-item             = type-alias | trait-definition | struct-definition | class-definition | implementation-definition | function-definition | decorated-function-definition | external | top-level-expression ;
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
 expression      = unary-expression binary-operator-right ;
 binary-operator-right        = { binary-operator unary-expression } ;
 lvalue          = name | field-access | index-expression ;
 variable-binding      = name ":" type [ "=" expression ] ;
 unary-expression       = unary-operator unary-expression | postfix-expression ;
 unary-operator         = "-" | "!" | "~" | "++" | "--" | user-defined-unary-operator ;
 postfix-expression     = primary [ postfix-operator ] ;
 postfix-operator       = "++" | "--" ;
-primary         = cast-expression | sizeof-expression | address-expression | array-literal | string-literal | name-expression | field-access | index-expression | number-expression | boolean-literal | parenthesized-expression ;
+primary         = cast-expression | sizeof-expression | address-expression | array-literal | string-literal | character-literal | name-expression | field-access | index-expression | number-expression | boolean-literal | parenthesized-expression ;
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
+character-literal     = "'" ( ? any char except ' and newline ? | character-escape ) "'" ;
 escape          = "\\" ( "\\" | "\"" | "n" | "t" | "0" ) ;
+character-escape      = "\\" ( "\\" | "'" | "n" | "t" | "0" ) ;
 parenthesized-expression       = "(" expression ")" ;
 binary-operator        = builtin-binary-operator | user-defined-binary-operator ;
 indent          = INDENT ;
 dedent          = DEDENT ;

 assignment-operator        = "=" | "+=" | "-=" | "*=" | "/=" | "%=" ;
 builtin-binary-operator = "+" | "-" | "*" | "/" | "%"
                 | "<" | "<=" | ">" | ">=" | "==" | "!="
                 | "&&" | "||"
                 | "&" | "|" | "^" | "<<" | ">>" ;
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

## New Token and Storage Global

I'll add one new character token:

```cpp
tok_char = -64,
```

I'll store the character's integer value in a new global before returning the token:

```cpp
static uint32_t CharLiteralValue = 0; // Filled in if tok_char
```

If you recall, this is similar to what I did for names and numbers. 

## Lexer: Scanning the Character Literal

When I see `'`, I'll read the character content, check for the closing `'`, and set `CharLiteralValue`:

```cpp
if (LexerLastChar == '\'') {
  LexerLastChar = advance(); // eat opening quote
  if (LexerLastChar == '\'' || LexerLastChar == '\n' ||
      LexerLastChar == EOF) {
    fprintf(stderr, "Error (Line %d, Column %d): empty character literal\n",
            CurLoc.Line, CurLoc.Col);
    PrintErrorSourceContext(CurLoc);
    return tok_error;
  }

  uint32_t Value = 0;
  if (LexerLastChar == '\\') {
    LexerLastChar = advance();
    switch (LexerLastChar) {
    case '\\':
      Value = '\\';
      break;
    case '\'':
      Value = '\'';
      break;
    case 'n':
      Value = '\n';
      break;
    case 't':
      Value = '\t';
      break;
    case '0':
      Value = '\0';
      break;
    default:
      fprintf(stderr,
              "Error (Line %d, Column %d): invalid character escape\n",
              CurLoc.Line, CurLoc.Col);
      PrintErrorSourceContext(CurLoc);
      return tok_error;
    }
  } else {
    Value = static_cast<unsigned char>(LexerLastChar);
  }

  LexerLastChar = advance();
  if (LexerLastChar != '\'') {
    fprintf(stderr,
            "Error (Line %d, Column %d): unterminated character literal\n",
            CurLoc.Line, CurLoc.Col);
    PrintErrorSourceContext(CurLoc);
    return tok_error;
  }
  LexerLastChar = advance(); // eat closing quote
  CharLiteralValue = Value;
  return tok_char;
}
```

I'll support five escapes: `\\`, `\'`, `\n`, `\t`, and `\0` — the same ones C uses.  Anything other than these is a `tok_error`.

## Error Cases

**Invalid escape sequence:**
```pyxc
var x: int32 = '\x'  # Error: invalid character escape
```

**Empty literal:**
```pyxc
var x: int32 = ''    # Error: empty character literal
```

**Unterminated literal:**
```pyxc
var x: int32 = 'a    # Error: unterminated character literal
```

**Value out of range for type:** a bare character stores its raw byte value, so any byte from 128–255 (outside ASCII) exceeds `int8`'s signed maximum of 127:
```
Error (Line 2, Column 17): Character literal out of range for type
```
This one is awkward to type into an example — there's no `\xNN` escape (only the five in the table above), so triggering it means a raw byte ≥ 128 actually sitting between the quotes, which ordinary UTF-8 source text won't produce.

## Building the AST Node

Whenever I see `CurrentToken == tok_char` in `ParsePrimary()`, I'll parse the character literal into a `NumberExpressionNode` because a character literal is just an integer. I'll call the parsing function `ParseCharExpression()`. 

```cpp
static unique_ptr<ExpressionNode> ParsePrimary() {
  switch (CurrentToken) {
  ...
  case tok_char:
    return ParseCharExpression();    
  }
  ...
}
```

In the parsing function, I default to `Int32`, matching `getchar()`'s return type and C's `int`. If the surrounding context (from `ExpectedLiteralTypeGuard`, the same context-communicating global I introduced back in Chapter 17) expects a different integer type — say `var c: int8 = 'A'` — I adopt that type instead, with a range check against the target's maximum. A character value that doesn't fit in the target width is a parse error. 

```cpp
static unique_ptr<ExpressionNode> ParseCharExpression() {
  ValueType Type = ValueType::Int32;
  if (IsIntType(ExpectedLiteralType))
    Type = ExpectedLiteralType;
  unsigned Bits = LLVMTypeFor(Type)->getIntegerBitWidth();
  APInt Max = APInt::getSignedMaxValue(Bits);
  APInt Val(std::max(1u, Bits), CharLiteralValue, false);
  if (Val.ugt(Max))
    return LogErrorExpression("Character literal out of range for type");
  if (Val.getBitWidth() != Bits)
    Val = Val.trunc(Bits);
  auto Result = make_unique<NumberExpressionNode>(Val, Type);
  getNextToken(); // consume the character literal
  return Result;
}
```

## What's Next

[Chapter 39](chapter-39.md) adds unsigned integer types: `uint8`, `uint16`, `uint32`, and `uint64`.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.
