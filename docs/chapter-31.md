---
description: "Add character literals so single characters can be written as 'a', '\n', '\t', and '\\' instead of their numeric ASCII values."
---
# 31. pyxc: Character Literals

## What I Am Building

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
cd pyxc-llvm-tutorial/code/chapter-31
```

## Grammar

I add `character-literal` as a `primary` alternative, and two new productions for its content:

`code/chapter-31/pyxc.ebnf`

```grammardiff
*...
*                                    | array-literal
*                                    | string-literal
+                                    | character-literal
*                                    | name-expression
*                                    | number-expression
*...
*escape                            = "\\" ( "\\" | '"' | "n" | "t" | "0" ) ;
*string-character                  = ? any character except '"', "\\", "\r", and "\n" ? ;
+character-literal                 = "'" ( character | character-escape ) "'" ;
+character-escape                  = "\\" ( "\\" | "'" | '"' | "?"
+                                      | "a" | "b" | "f" | "n" | "r"
+                                      | "t" | "v" | "0"
+                                      | "x" hex-digit hex-digit ) ;
+character                         = ? any character except "'", "\\", "\r", and "\n" ? ;
+hex-digit                         = digit | "A".."F" | "a".."f" ;
*name-expression                   = lvalue | call-expression ;
*call-expression                   = name "(" [ arguments ] ")" ;
*...
```

## New Token and Storage Global

I'll add one new character token:

```cppdiff
 enum Token {
*  ...
*  tok_type = -54,
*  tok_string = -55,
+  tok_character = -56,
*
*  // punctuation and operators
*  tok_lparen = '(',
*  ...
*};
```

I'll store the character's integer value in a new global before returning the token:

```cpp
static uint32_t CharacterLiteralValue = 0;
```

If you recall, this is similar to what I did for names and numbers. 

## Lexer: Scanning the Character Literal

When I see `'`, I'll read the character content, check for the closing `'`, and set `CharacterLiteralValue`:

```cpp
if (LexerLastChar == '\'') {
  auto HexDigitValue = [](int Character) -> int {
    if (Character >= '0' && Character <= '9')
      return Character - '0';
    if (Character >= 'a' && Character <= 'f')
      return Character - 'a' + 10;
    if (Character >= 'A' && Character <= 'F')
      return Character - 'A' + 10;
    return -1;
  };

  LexerLastChar = advance(); // eat opening quote
  if (LexerLastChar == '\'') {
    fprintf(stderr, "Error (Line %d, Column %d): empty character literal\n",
            CurrentTokenLocation.Line, CurrentTokenLocation.Column);
    PrintErrorSourceContext(CurrentTokenLocation);
    return tok_error;
  }
  if (LexerLastChar == EOF || LexerLastChar == '\n') {
    fprintf(stderr,
            "Error (Line %d, Column %d): unterminated character literal\n",
            CurrentTokenLocation.Line, CurrentTokenLocation.Column);
    PrintErrorSourceContext(CurrentTokenLocation);
    return tok_error;
  }

  if (LexerLastChar == '\\') {
    LexerLastChar = advance();
    bool HexEscape = false;
    switch (LexerLastChar) {
    case '\\': CharacterLiteralValue = '\\'; break;
    case '\'': CharacterLiteralValue = '\''; break;
    case '"': CharacterLiteralValue = '"'; break;
    case '?': CharacterLiteralValue = '?'; break;
    case 'a': CharacterLiteralValue = 7; break;
    case 'b': CharacterLiteralValue = 8; break;
    case 'f': CharacterLiteralValue = 12; break;
    case 'n': CharacterLiteralValue = 10; break;
    case 'r': CharacterLiteralValue = 13; break;
    case 't': CharacterLiteralValue = 9; break;
    case 'v': CharacterLiteralValue = 11; break;
    case '0': CharacterLiteralValue = 0; break;
    case 'x': {
      HexEscape = true;
      int High = HexDigitValue(advance());
      int Low = HexDigitValue(advance());
      // Fewer than two hex digits after \x, e.g. var b: int32 = '\x'
      if (High < 0 || Low < 0) {
        fprintf(stderr,
                "Error (Line %d, Column %d): invalid character escape\n",
                CurrentTokenLocation.Line, CurrentTokenLocation.Column);
        PrintErrorSourceContext(CurrentTokenLocation);
        return tok_error;
      }
      CharacterLiteralValue = static_cast<uint32_t>((High << 4) | Low);
      LexerLastChar = advance();
      break;
    }
    default:
      // Backslash followed by a letter that isn't one of the escapes above,
      // e.g. var q: int32 = '\q'
      fprintf(stderr,
              "Error (Line %d, Column %d): invalid character escape\n",
              CurrentTokenLocation.Line, CurrentTokenLocation.Column);
      PrintErrorSourceContext(CurrentTokenLocation);
      return tok_error;
    }
    if (!HexEscape)
      LexerLastChar = advance();
  } else {
    CharacterLiteralValue = static_cast<unsigned char>(LexerLastChar);
    LexerLastChar = advance();
  }

  if (LexerLastChar != '\'') {
    // More than one character between the quotes, e.g. 'ab', falls here too,
    // distinguished from an unterminated literal by not hitting EOF/newline.
    const char *Message =
        (LexerLastChar == EOF || LexerLastChar == '\n')
            ? "unterminated character literal"
            : "character literal must contain one character";
    fprintf(stderr, "Error (Line %d, Column %d): %s\n", CurrentTokenLocation.Line,
            CurrentTokenLocation.Column, Message);
    PrintErrorSourceContext(CurrentTokenLocation);
    return tok_error;
  }
  LexerLastChar = advance(); // eat closing quote
  return tok_character;
}
```

I'll support all eleven of C's [simple escape sequences](https://en.cppreference.com/c/language/escape): `\a`, `\b`, `\f`, `\n`, `\r`, `\t`, `\v`, `\\`, `\'`, `\"`, and `\?`. I take two deliberate departures from that reference, though. C's numeric escapes are `\nnn` (an arbitrary-length octal value) and `\xn...` (an arbitrary-length hex value); I only keep `\0` as a fixed single-character case for the null byte, not general octal, and I require `\xNN` to be exactly two hex digits rather than an open-ended run. C's universal character names, `\unnnn` and `\Unnnnnnnn`, aren't supported at all yet, those arrive in [Chapter 32](chapter-32.md). Anything else is a `tok_error`, and so is a literal holding more than one character, like `'ab'`.

## Building the AST Node

Whenever I see `CurrentToken == tok_character` in `ParsePrimary()`, I'll parse the character literal into a `NumberExpressionNode` because a character literal is just an integer. I'll call the parsing function `ParseCharacterExpression()`. 

```cppdiff
 static unique_ptr<ExpressionNode> ParsePrimary() {
*  switch (CurrentToken) {
*  case tok_number:
*    return ParseNumberExpression();
*  case tok_name:
*    return ParseNameExpression();
*  case tok_string: {
*    string Text = StringLiteralValue;
*    getNextToken();
*    return make_unique<StringExpressionNode>(
*        std::move(Text), EncodePointerType(ValueType::Int8));
*  }
+  case tok_character:
+    return ParseCharacterExpression();
*  case tok_true:
*    getNextToken();
*    return make_unique<BoolExpressionNode>(true);
*  ...
*  }
*}
```

In the parsing function, I default to `Int32`, matching `getchar()`'s return type and C's `int`. If the surrounding context (from `ExpectedLiteralTypeGuard`, the same context-communicating global I introduced back in Chapter 18) expects a different integer type — say `var c: int8 = 'A'` — I adopt that type instead, with a range check against the target's maximum. A character value that doesn't fit in the target width is a parse error. 

```cpp
static unique_ptr<ExpressionNode> ParseCharacterExpression() {
  ValueType Type = IsIntType(ExpectedLiteralType)
                       ? ExpectedLiteralType
                       : ValueType::Int32;
  unsigned Bits = LLVMTypeFor(Type)->getIntegerBitWidth();
  uint64_t Maximum = IsUnsignedIntType(Type)
                         ? APInt::getMaxValue(Bits).getZExtValue()
                         : APInt::getSignedMaxValue(Bits).getZExtValue();
  if (CharacterLiteralValue > Maximum)
    return LogErrorExpression("Character literal out of range for type");
  auto Result = make_unique<NumberExpressionNode>(
      APInt(Bits, CharacterLiteralValue), Type);
  getNextToken(); // eat character literal
  return Result;
}
```

The maximum I check against depends on whether the target type is signed: `IsUnsignedIntType(Type)` picks `APInt::getMaxValue(Bits)` (all bits set) for an unsigned target and `APInt::getSignedMaxValue(Bits)` for a signed one, so `'\x80'` (128) is in range for `uint8` but out of range for `int8`, whose signed maximum is 127.

## Build and Run

```bash
cd code/chapter-31
cmake -S . -B build && cmake --build build
./build/pyxc
```

```bash
llvm-lit -v test/
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
Error (Line 6, Column 17): unterminated character literal
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

The remaining four lines trigger the error checks called out above: `'\x80'` trips the range check in `ParseCharacterExpression` since 128 exceeds `int8`'s signed max, `''` trips the empty-literal check, `'\x'` trips the bad-hex-digit check, and the unclosed `'a` trips the missing-closing-quote check. Each lexer-level error is followed by a second "unknown token when expecting an expression" line: once the lexer returns `tok_error`, the parser reports its own failure to parse an expression from that token. For `''`, the REPL's line-recovery logic then re-scans starting right after the closing quote it just consumed, lands on the trailing newline, and reports that as its own unterminated character literal, which is why that case reports three errors instead of two.

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

I'll help you figure it out.
