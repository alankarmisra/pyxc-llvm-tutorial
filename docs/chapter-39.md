---
description: "Decode Unicode escapes and raw UTF-8 in character and string literals while rejecting invalid Unicode values."
---
# 39. pyxc: Unicode Literals

## What I Am Building

[Chapter 38](chapter-38.md) added character literals and byte-sized hexadecimal escapes. I can write `'A'`, `'\n'`, and `'\x41'`, but I cannot write a character such as `Ω` or `🙂` yet. String literals copy non-ASCII bytes without checking whether those bytes form valid UTF-8.

In this chapter, I make both literal forms understand Unicode:

```pyxc
var omega: int32 = 'Ω'
var smile: int32 = '\U0001F642'
puts("caf\u00E9 Ω 🙂")
```

I'm only adding Unicode to character and string literals here. Unicode identifiers — `café` as a variable name — are a separate problem with their own rules; I'm leaving that for later.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-39
```

## Grammar

I replace the separate string and character escape productions with one `literal-escape` production. I also add octal, `\u`, and `\U` forms:

`code/chapter-39/pyxc.ebnf`

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
-string-literal   = "\"" { ? any char except " and newline ? | escape } "\"" ;
-character-literal     = "'" ( ? any char except ' and newline ? | character-escape ) "'" ;
-escape          = "\\" ( "\\" | "\"" | "n" | "t" | "0" ) ;
-character-escape      = "\\" ( "a" | "b" | "f" | "n" | "r" | "t" | "v"
-                        | "\\" | "'" | "\"" | "?" | "0" | "x" hex-digit hex-digit ) ;
+string-literal   = "\"" { ? valid Unicode scalar value except " and newline, encoded as UTF-8 ? | literal-escape } "\"" ;
+character-literal     = "'" ( ? valid Unicode scalar value except ' and newline, encoded as UTF-8 ? | literal-escape ) "'" ;
+literal-escape   = "\\" ( simple-escape | octal-escape | "x" hex-digit hex-digit
+                   | "u" hex-digit hex-digit hex-digit hex-digit
+                   | "U" hex-digit hex-digit hex-digit hex-digit
+                         hex-digit hex-digit hex-digit hex-digit ) ;
+simple-escape    = "a" | "b" | "f" | "n" | "r" | "t" | "v"
+                 | "\\" | "'" | "\"" | "?" ;
+octal-escape     = octal-digit [ octal-digit [ octal-digit ] ] ;
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
 hex-digit       = digit | "A".."F" | "a".."f" ;
+octal-digit     = "0".."7" ;
 end-of-line             = "\r\n" | "\r" | "\n" ;
 comment = "#" { comment-character } ;
 comment-character = ? any character except "\r" and "\n" ? ;
 whitespace = " " | "\t" | "\v" | "\f" ;
 INDENT          = ? synthetic token emitted by lexer ? ;
 DEDENT          = ? synthetic token emitted by lexer ? ;
 
 BLOCK_END = ? synthetic token injected into the stream by ParseBlock immediately after it consumes DEDENT ? ;
```

I keep `\xNN` at exactly two hexadecimal digits, as I defined it in Chapter 38. I let an octal escape consume one, two, or three digits. I give `\u` a fixed four hex digits and `\U` a fixed eight.

## Code Points and UTF-8

I decode either literal down to one code point. For a character literal, that code point is the value. For a string literal, I go one step further and encode it as UTF-8 bytes.

Unicode only defines code points through `U+10FFFF`. The range `U+D800` through `U+DFFF` is reserved for UTF-16 surrogate pairs, so those values are not standalone Unicode characters. I reject both cases with one check:

```cpp
static bool IsUnicodeScalarValue(uint32_t Value) {
  return Value <= 0x10FFFF && !(Value >= 0xD800 && Value <= 0xDFFF);
}
```

The valid values are called Unicode scalar values.

## Sharing One Decoder

Strings and characters now accept the same escape forms and the same raw UTF-8. I use one result type for the failures that can occur along the way:

```cpp
enum class LiteralDecodeError {
  None,
  InvalidEscape,
  InvalidUtf8,
  InvalidCodePoint,
};
```

I then send both literal paths through `DecodeLiteralCodePoint()`. The function reads either one escape or one raw UTF-8 sequence, returns its code point through `Value`, and leaves `LexerLastChar` at the first byte after it.

### Decoding Escapes

The existing simple escapes each become their corresponding code point. I parse `\xNN` as two hexadecimal digits. Anything else falls to a default case: if it's not an octal digit either, it's not a valid escape at all — `\q` or `\8` both land here and get rejected. Otherwise I consume up to three octal digits:

```cpp
default:
  if (LexerLastChar < '0' || LexerLastChar > '7')
    return LiteralDecodeError::InvalidEscape;

  Value = 0;
  for (int I = 0; I < 3; ++I) {
    Value = (Value << 3) |
            static_cast<uint32_t>(LexerLastChar - '0');
    int Next = peek();
    if (I == 2 || Next < '0' || Next > '7') {
      LexerLastChar = advance();
      break;
    }
    LexerLastChar = advance();
  }
  return LiteralDecodeError::None;
```

`'\101'` is octal for 65 — the letter `A`.

For `\u` and `\U`, I read exactly four or eight hexadecimal digits and then validate the result:

```cpp
case 'u':
case 'U': {
  int Digits = LexerLastChar == 'u' ? 4 : 8;
  Value = 0;
  for (int I = 0; I < Digits; ++I) {
    int Digit = HexDigitValue(advance());
    if (Digit < 0)
      return LiteralDecodeError::InvalidEscape;
    Value = (Value << 4) | static_cast<uint32_t>(Digit);
  }
  LexerLastChar = advance();
  if (!IsUnicodeScalarValue(Value))
    return LiteralDecodeError::InvalidCodePoint;
  return LiteralDecodeError::None;
}
```

So `\u03A9` gives me `Ω`, and `\U0001F642` gives me `🙂`.

**Incomplete escape:**
```pyxc
var x: int32 = '\u123'
```
```
Error (Line 2, Column 18): invalid character escape
  var x: int32 = '\u123'
                 ^~~~
```

**Surrogate value:**
```pyxc
var x: int32 = '\uD800'
```
```
Error (Line 2, Column 18): invalid Unicode code point in character literal
  var x: int32 = '\uD800'
                 ^~~~
```

### Decoding Raw UTF-8

For a raw non-ASCII character, I inspect the leading byte to decide whether the sequence contains two, three, or four bytes. I then require every remaining byte to have the UTF-8 continuation-byte shape `10xxxxxx`:

```cpp
for (int I = 1; I < Length; ++I) {
  int Next = advance();
  if (Next == EOF || (Next & 0xC0) != 0x80)
    return LiteralDecodeError::InvalidUtf8;
  Value = (Value << 6) | static_cast<uint32_t>(Next & 0x3F);
}
LexerLastChar = advance();
```

I also reject invalid leading bytes, overlong encodings, surrogate values, and values above `U+10FFFF`. A stray continuation byte on its own — one that never follows a valid leading byte — hits that same rejection:
```
Error (Line 2, Column 18): invalid UTF-8 in character literal
```

Raw and escaped spellings reach the same validated code point:

```pyxc
'Ω' == '\u03A9'
'🙂' == '\U0001F642'
```

## Producing Character Values

The character-literal branch now asks the shared decoder for one code point:

```cpp
uint32_t Value = 0;
LiteralDecodeError Error = DecodeLiteralCodePoint(Value);
if (Error != LiteralDecodeError::None)
  return LogLiteralDecodeError(Error, "character");

if (LexerLastChar != '\'') {
  fprintf(stderr,
          "Error (Line %d, Column %d): unterminated character literal\n",
          CurLoc.Line, CurLoc.Col);
  PrintErrorSourceContext(CurLoc);
  return tok_error;
}
```

I still turn `CharLiteralValue` into a `NumberExpressionNode`, same as before. A Unicode character stays an integer, so it goes through the same range checks every other character literal already does.

## Producing UTF-8 Strings

For a string, I decode one code point at a time and append its UTF-8 encoding:

```cpp
while (LexerLastChar != '"' && LexerLastChar != EOF &&
       LexerLastChar != '\n') {
  uint32_t Value = 0;
  LiteralDecodeError Error = DecodeLiteralCodePoint(Value);
  if (Error != LiteralDecodeError::None)
    return LogLiteralDecodeError(Error, "string");
  AppendUtf8(StringLiteralStr, Value);
}
```

`AppendUtf8()` emits one byte for ASCII and two, three, or four bytes for larger code points:

```cpp
static void AppendUtf8(string &Output, uint32_t Value) {
  if (Value <= 0x7F) {
    Output.push_back(static_cast<char>(Value));
  } else if (Value <= 0x7FF) {
    Output.push_back(static_cast<char>(0xC0 | (Value >> 6)));
    Output.push_back(static_cast<char>(0x80 | (Value & 0x3F)));
  } else if (Value <= 0xFFFF) {
    Output.push_back(static_cast<char>(0xE0 | (Value >> 12)));
    Output.push_back(static_cast<char>(0x80 | ((Value >> 6) & 0x3F)));
    Output.push_back(static_cast<char>(0x80 | (Value & 0x3F)));
  } else {
    Output.push_back(static_cast<char>(0xF0 | (Value >> 18)));
    Output.push_back(static_cast<char>(0x80 | ((Value >> 12) & 0x3F)));
    Output.push_back(static_cast<char>(0x80 | ((Value >> 6) & 0x3F)));
    Output.push_back(static_cast<char>(0x80 | (Value & 0x3F)));
  }
}
```

Raw UTF-8 goes through this same decode-and-encode path — I validate it now instead of just copying it blindly.

## Try It

```pyxc
extern def puts(s: ptr[int8]) -> int

def main() -> int:
  puts("caf\u00E9")
  puts("Ω 🙂")
  return 0
```

```
café
Ω 🙂
```

## Known Limitations

**Identifiers are still ASCII-only.** `var café: int` doesn't work; Unicode in variable, function, struct, or class names is a separate problem I'm leaving for later.

**No Unicode normalization.** Two visually identical strings that use different Unicode representations (e.g. precomposed vs. combining-character forms) are different byte sequences to pyxc; there's no NFC/NFD normalization step.

## Build and Run

```bash
cd code/chapter-39
cmake -S . -B build && cmake --build build
```

## What's Next

[Chapter 40](chapter-40.md) adds unsigned integer types: `uint8`, `uint16`, `uint32`, and `uint64`.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.
