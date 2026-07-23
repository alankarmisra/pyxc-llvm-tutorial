---
description: "Add character literals so single characters can be written as 'a', '\n', '\t', and '\\' instead of their numeric ASCII values."
---
# 38. pyxc: Character Literals

## Where We Are

[Chapter 37](chapter-37.md) added `elif`. pyxc can call C library functions like `getchar()`, but comparing the result to a space or newline requires knowing the ASCII value off the top of your head:

```pyxc
if c == 32:   # space
if c == 10:   # newline — or was it 13?
```

After this chapter, you can write what you mean:

```pyxc
if c == ' ':
if c == '\n':
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-38
```

## New Token and Storage Global

One new token:

```cpp
tok_char = -64,
```

The lexer stores the character's integer value in a new global before returning the token:

```cpp
static uint32_t CharLiteralValue = 0; // Filled in if tok_char
```

## Lexer: Scanning the Character Literal

When the lexer sees `'`, it reads the character content, checks for the closing `'`, and sets `CharLiteralValue`:

```cpp
if (LexerLastChar == '\'') {
  LexerLastChar = advance(); // eat opening quote
  if (LexerLastChar == '\'' || LexerLastChar == '\n' || LexerLastChar == EOF) {
    // error: empty or unterminated
    return tok_error;
  }

  uint32_t Value = 0;
  if (LexerLastChar == '\\') {
    LexerLastChar = advance();
    switch (LexerLastChar) {
    case '\\': Value = '\\'; break;
    case '\'': Value = '\''; break;
    case 'n':  Value = '\n'; break;
    case 't':  Value = '\t'; break;
    case '0':  Value = '\0'; break;
    default:
      // error: invalid character escape
      return tok_error;
    }
  } else {
    Value = static_cast<unsigned char>(LexerLastChar);
  }

  LexerLastChar = advance();
  if (LexerLastChar != '\'') {
    // error: unterminated character literal
    return tok_error;
  }
  LexerLastChar = advance(); // eat closing quote
  CharLiteralValue = Value;
  return tok_char;
}
```

The five escape sequences:

| Written | Value | Meaning |
|---|---|---|
| `'\\'` | 92 | backslash |
| `'\''` | 39 | single quote |
| `'\n'` | 10 | newline |
| `'\t'` | 9 | horizontal tab |
| `'\0'` | 0 | null byte |

A bare character (no backslash) stores its unsigned byte value via `static_cast<unsigned char>`. Any backslash sequence other than the five listed is a `tok_error`.

## `ParseCharExpr` — Building the AST Node

`ParseCharExpr` is called from the primary expression dispatcher when `CurTok == tok_char`. It reuses `NumberExprAST` — a character literal is just an integer constant:

```cpp
static unique_ptr<ExprAST> ParseCharExpr() {
  ValueType Type = ValueType::Int32;
  if (IsIntType(ExpectedLiteralType))
    Type = ExpectedLiteralType;
  unsigned Bits = LLVMTypeFor(Type)->getIntegerBitWidth();
  APInt Max = IsUnsignedIntType(Type) ? APInt::getAllOnes(Bits)
                                      : APInt::getSignedMaxValue(Bits);
  APInt Val(std::max(1u, Bits), CharLiteralValue, false);
  if (Val.ugt(Max))
    return LogError("Character literal out of range for type");
  if (Val.getBitWidth() != Bits)
    Val = Val.trunc(Bits);
  auto Result = make_unique<NumberExprAST>(Val, Type);
  getNextToken(); // consume tok_char
  return Result;
}
```

The default type is `Int32`, matching `getchar()`'s return type and C's `int`. If the surrounding context (from `ExpectedLiteralTypeGuard`) expects a different integer type — say `var c: int8 = 'A'` — the literal adopts that type, with a range check against the target's maximum. A character value that doesn't fit in the target width is a parse error.

## `IsAssignable` Widening Fix

This chapter also removes the old `IsFixedIntType` / `FixedIntRank` helper pair and replaces the integer widening check with a direct bit-width comparison that works for all integer types, including the unsigned types added next chapter:

```cpp
// Before (ch36 and earlier):
static bool IsFixedIntType(ValueType Type) {
  return Type == ValueType::Int8 || Type == ValueType::Int16 ||
         Type == ValueType::Int32 || Type == ValueType::Int64;
}
static int FixedIntRank(ValueType Type) { /* 1–4 */ }

// Now (ch37 onward):
if (IsIntType(From) && IsIntType(To)) {
  unsigned FromBits = LLVMTypeFor(From)->getIntegerBitWidth();
  unsigned ToBits   = LLVMTypeFor(To)->getIntegerBitWidth();
  return FromBits <= ToBits;
}
```

Using the LLVM type's bit width means the same code works for signed and unsigned integers without a separate rank table.

## Primary Expression Dispatch

`tok_char` is wired into `ParsePrimary`:

```cpp
case tok_char:
  return ParseCharExpr();
```

## Grammar

```ebnf
primary     = castexpr | sizeofexpr | addrexpr | arrayliteral | stringliteral
            | charliteral | identifierexpr | fieldaccess | indexexpr  -- changed
            | numberexpr | bool_literal | parenexpr ;
charliteral = "'" ( ? any char except ' and newline ? | charescape ) "'" ; -- new
charescape  = "\\" ( "\\" | "'" | "n" | "t" | "0" ) ;                      -- new
```

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

**Value out of range for type:**
```pyxc
var c: int8 = '\xFF'  # Error: Character literal out of range for type
```

## Things Worth Knowing

**A character literal is just an integer.** `'a' + 1` is `98`. `'z' - 'a'` is `25`. Arithmetic on character values works exactly as it does in C.

**The default type is `int32`, not `int8`.** This matches `getchar()`, which returns `int32` to distinguish `EOF` (−1) from a valid byte (0–255). If you store into an `int8`, values above 127 will be negative.

**No multi-character literals.** `'ab'` is not valid. Use string literals for strings.

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
