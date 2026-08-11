---
description: "Add character literals so single characters can be written as 'a', '\n', '\t', and '\\' instead of their numeric ASCII values."
---
# 31. pyxc: Character Literals

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
+character-escape      = "\\" ( "a" | "b" | "f" | "n" | "r" | "t" | "v"
+                        | "\\" | "'" | "\"" | "?" | "0" | "x" hex-digit hex-digit ) ;
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
+hex-digit       = digit | "A".."F" | "a".."f" ;
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
  // Nothing between the quotes, e.g. var e: int32 = ''
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
      case 'a':
        Value = '\a';
        break;
      case 'b':
        Value = '\b';
        break;
      case 'f':
        Value = '\f';
        break;
      case 'n':
        Value = '\n';
        break;
      case 'r':
        Value = '\r';
        break;
      case 't':
        Value = '\t';
        break;
      case 'v':
        Value = '\v';
        break;
      case '\\':
        Value = '\\';
        break;
      case '\'':
        Value = '\'';
        break;
      case '"':
        Value = '"';
        break;
      case '?':
        Value = '?';
        break;
      case '0':
        Value = '\0';
        break;
      case 'x': {
        auto HexDigitValue = [](int Ch) -> int {
          if (Ch >= '0' && Ch <= '9')
            return Ch - '0';
          if (Ch >= 'a' && Ch <= 'f')
            return Ch - 'a' + 10;
          if (Ch >= 'A' && Ch <= 'F')
            return Ch - 'A' + 10;
          return -1;
        };

        int High = HexDigitValue(advance());
        int Low = HexDigitValue(advance());
        // Fewer than two hex digits after \x, e.g. var b: int32 = '\x'
        if (High < 0 || Low < 0) {
          fprintf(stderr,
                  "Error (Line %d, Column %d): invalid character escape\n",
                  CurLoc.Line, CurLoc.Col);
          PrintErrorSourceContext(CurLoc);
          return tok_error;
        }
        Value = static_cast<uint32_t>((High << 4) | Low);
        break;
      }
    default:
      // Backslash followed by a letter that isn't one of the escapes above,
      // e.g. var q: int32 = '\q'
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
  // No closing quote where one is expected, e.g. var u: int32 = 'a
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

I'll support all eleven of C's [simple escape sequences](https://en.cppreference.com/c/language/escape): `\a`, `\b`, `\f`, `\n`, `\r`, `\t`, `\v`, `\\`, `\'`, `\"`, and `\?`. I take two deliberate departures from that reference, though. C's numeric escapes are `\nnn` (an arbitrary-length octal value) and `\xn...` (an arbitrary-length hex value); I only keep `\0` as a fixed single-character case for the null byte, not general octal, and I require `\xNN` to be exactly two hex digits rather than an open-ended run. C's universal character names, `\unnnn` and `\Unnnnnnnn`, aren't supported at all yet, those arrive in [Chapter 32](chapter-32.md). Anything else is a `tok_error`.

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

In the parsing function, I default to `Int32`, matching `getchar()`'s return type and C's `int`. If the surrounding context (from `ExpectedLiteralTypeGuard`, the same context-communicating global I introduced back in Chapter 18) expects a different integer type — say `var c: int8 = 'A'` — I adopt that type instead, with a range check against the target's maximum. A character value that doesn't fit in the target width is a parse error. 

```cpp
static unique_ptr<ExpressionNode> ParseCharExpression() {
  ValueType Type = ValueType::Int32;
  if (IsIntType(ExpectedLiteralType))
    Type = ExpectedLiteralType;
  unsigned Bits = LLVMTypeFor(Type)->getIntegerBitWidth();
  APInt Max = APInt::getSignedMaxValue(Bits);
  APInt Val(std::max(1u, Bits), CharLiteralValue, false);
  // A bare character stores its raw byte value, so any byte from 128-255
  // (outside ASCII) exceeds int8's signed maximum of 127. Trigger this with
  // a hex escape, e.g. var c: int8 = '\x80'.
  if (Val.ugt(Max))
    return LogErrorExpression("Character literal out of range for type");
  if (Val.getBitWidth() != Bits)
    Val = Val.trunc(Bits);
  auto Result = make_unique<NumberExpressionNode>(Val, Type);
  getNextToken(); // consume the character literal
  return Result;
}
```

## Try It

<!-- code-merge:start -->
```pyxc
ready> 'a' == 97
```
```text
True
```
```pyxc
ready> '\n' == 10
```
```text
True
```
```pyxc
ready> var c: int8 = 'A'
ready> c
```
```text
65
```
```pyxc
ready> var d: int8 = '\x80'
```
```text
Error (Line 5, Column 15): Character literal out of range for type
var d: int8 = '\x80'
              ^~~~
```
```pyxc
ready> var e: int32 = ''
```
```text
Error (Line 6, Column 16): empty character literal
var e: int32 = ''
               ^~~~
Error (Line 6, Column 16): unknown token when expecting an expression
var e: int32 = ''
               ^~~~
Error (Line 6, Column 17): empty character literal
var e: int32 = ''
                ^~~~
```
```pyxc
ready> var b: int32 = '\x'
```
```text
Error (Line 7, Column 16): invalid character escape
var b: int32 = '\x'
               ^~~~
Error (Line 7, Column 16): unknown token when expecting an expression
var b: int32 = '\x'
               ^~~~
```
```pyxc
ready> var u: int32 = 'a
```
```text
Error (Line 8, Column 16): unterminated character literal
var u: int32 = 'a
               ^~~~
```
<!-- code-merge:end -->

`'a'` compares equal to its ASCII code without me writing the number out, `'\n'` resolves to `10` through the escape path, and `'A'` assigned into an `int8` carries its value through untruncated since 65 fits comfortably under `int8`'s signed max of 127.

The remaining four lines trigger the error checks called out above: `'\x80'` trips the range check in `ParseCharExpression` since 128 exceeds `int8`'s signed max, `''` trips the empty-literal check, `'\x'` trips the bad-hex-digit check, and the unclosed `'a` trips the missing-closing-quote check. Each lexer-level error (the last three) is followed by a second "unknown token when expecting an expression" line: once the lexer returns `tok_error`, the parser reports its own failure to parse an expression from that token, and the REPL's line-recovery logic re-scans the remainder of the input, which is why the empty-literal case reports the same error twice more.

## What's Next

[Chapter 32](chapter-32.md) adds Unicode escapes and validated UTF-8.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.
