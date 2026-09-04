---
section: "Foundations"
description: "Add buffered source lines, precise locations, caret diagnostics, and line-based error recovery."
---

# 5. pyxc: Better Errors

Next: make a parser failure tell the reader what went wrong and where.

Replace diagnostics such as:

```text
Error: Expected ':'
```

with:

```text
Error (Line 1, Column 14): Expected ':' in function definition
def add(a, b)
             ^~~~
```

This chapter adds one diagnostic boundary:

```text
bad token -> message + line + column + source line + caret
```

Work in:

```bash
cd code/chapter-05
```

## 1. Let Character Tokens Name Themselves

Change punctuation and operator tokens so their enum values equal their character values:

```cpp
tok_lparen = '(',
tok_rparen = ')',
tok_comma = ',',
tok_colon = ':',
tok_plus = '+',
tok_minus = '-',
tok_star = '*',
tok_slash = '/',
tok_percent = '%',
tok_less = '<',
```

Then `getToken()` can return any single character directly:

```cpp
int ThisChar = LastChar;
LastChar = advance();
return ThisChar;
```

Known punctuation already has the correct token value. Unknown characters remain printable in diagnostics instead of collapsing into a generic `tok_error`.

Build a complete token-name table for byte values. Printable characters become quoted spellings; control characters become escaped names or hexadecimal values. This makes an unexpected `@` report `'@'`, not “unknown token.”

## 2. Add Source Locations

Add a one-based line/column pair:

```cpp
struct SourceLocation {
  int Line;
  int Column;
};

static SourceLocation CurrentTokenLocation;
static SourceLocation LexerLocation = {1, 0};
```

Keep two locations because they answer different questions:

```text
LexerLocation        -> where character reading has reached
CurrentTokenLocation -> where the current token began
```

The parser should diagnose `CurrentTokenLocation`, not wherever the lexer happens to be after reading the token.

## 3. Buffer Complete Source Lines

A caret diagnostic needs the complete line, including characters the lexer has not consumed yet. Add a `SourceManager` that owns:

```cpp
vector<string> Lines;
string BufferedLine;
size_t NextCharacter = 0;
bool HasBufferedNewline = false;
bool ReachedEndOfInput = false;
```

Its `readChar()` should:

1. Return remaining characters from `BufferedLine`.
2. Return one normalized `\n` after the line.
3. Read and store the next complete physical line.
4. Normalize `\n`, `\r\n`, and bare `\r`.
5. Return `EOF` only after buffered input is exhausted.

Add line lookup:

```cpp
const string *getLine(int OneBasedLine) const {
  if (OneBasedLine <= 0)
    return nullptr;

  size_t Index = static_cast<size_t>(OneBasedLine - 1);
  if (Index < Lines.size())
    return &Lines[Index];
  return nullptr;
}
```

Create one manager:

```cpp
static SourceManager PyxcSourceManager;
```

## 4. Move Newline Normalization into the Source Manager

Replace the old `getchar()`-based `advance()` with:

```cpp
static int advance() {
  int LastChar = PyxcSourceManager.readChar();

  if (LastChar == '\n') {
    LexerLocation.Line++;
    LexerLocation.Column = 0;
  } else if (LastChar != EOF) {
    LexerLocation.Column++;
  }

  return LastChar;
}
```

The source manager normalizes characters. `advance()` only updates position.

## 5. Snapshot Every Token Start

In `getToken()`, after skipping horizontal whitespace and before choosing a token branch, add:

```cpp
CurrentTokenLocation = LexerLocation;
```

For names and numbers, this is the location of their first character.

Newline tokens need special care: `advance()` has already incremented the line before `getToken()` returns `tok_eol`. Add a helper that anchors newline errors just after the previous line:

```cpp
static SourceLocation GetCaretAnchorLocation(SourceLocation Location,
                                              int Token) {
  if (Token != tok_eol || Location.Line <= 1)
    return Location;

  int PreviousLine = Location.Line - 1;
  const string *Text = PyxcSourceManager.getLine(PreviousLine);
  if (!Text)
    return Location;

  return {PreviousLine, static_cast<int>(Text->size()) + 1};
}
```

That makes a missing colon point where the colon should have appeared, rather than at column zero of the next line.

When comment handling consumes through a newline, re-snapshot `CurrentTokenLocation` before returning `tok_eol` so it follows the same rule.

## 6. Print the Source Context

Add:

```cpp
static void PrintErrorSourceContext(SourceLocation Location) {
  const string *LineText = PyxcSourceManager.getLine(Location.Line);
  if (!LineText)
    return;

  fprintf(stderr, "%s\n", LineText->c_str());

  int Spaces = Location.Column - 1;
  if (Spaces < 0)
    Spaces = 0;

  for (int I = 0; I < Spaces; ++I)
    fputc(' ', stderr);

  fprintf(stderr, "^~~~\n");
}
```

The location is one-based, so print `Column - 1` spaces before the caret.

## 7. Include Actual Token Spellings

Add:

```cpp
static string FormatTokenForMessage(int Token) {
  if (Token == tok_name)
    return "name '" + Name + "'";
  if (Token == tok_number)
    return "number '" + NumberLiteral + "'";

  auto It = TokenNames.find(Token);
  if (It != TokenNames.end())
    return It->second;
  return "unknown token";
}
```

Keep the original number spelling in:

```cpp
static string NumberLiteral;
```

`3.14` is more useful in a diagnostic than the generic word “number.”

## 8. Wire Locations into Parser Errors

Replace the old expression error helper with:

```cpp
unique_ptr<ExpressionNode>
LogErrorExpression(const string &ErrorMessage) {
  SourceLocation Anchor =
      GetCaretAnchorLocation(CurrentTokenLocation, CurrentToken);

  fprintf(stderr, "Error (Line %d, Column %d): %s\n",
          Anchor.Line, Anchor.Column, ErrorMessage.c_str());
  PrintErrorSourceContext(Anchor);
  return nullptr;
}
```

Keep signature and function-definition helpers delegating to this one.

Now every parser branch gets source context automatically:

```cpp
return LogErrorExpression("Expected ')'");
```

## 9. Reject the Entire Malformed Number

Chapter 1 used `strtod()` without checking where parsing stopped. Replace numeric conversion with:

```cpp
char *End = nullptr;
NumberValue = strtod(NumberLiteral.c_str(), &End);

if (!End || *End != '\0') {
  LogInvalidNumberLiteralAtLocation(NumberLiteral,
                                    CurrentTokenLocation);
  return tok_error;
}
```

Add the diagnostic helper:

```cpp
static void LogInvalidNumberLiteralAtLocation(
    const string &Literal, SourceLocation Location) {
  fprintf(stderr,
          "Error (Line %d, Column %d): invalid number literal '%s'\n",
          Location.Line, Location.Column, Literal.c_str());
  PrintErrorSourceContext(Location);
}
```

Now:

```pyxc
1.23.45
```

is one bad numeric token, not a valid prefix followed by confusing leftovers.

## 10. Recover at a Known Boundary

After a parse failure, skip the rest of the current line:

```cpp
static void DiscardRestOfLine() {
  while (CurrentToken != tok_eol && CurrentToken != tok_eof)
    getNextToken();
}
```

Call it after:

- A failed function definition.
- A failed top-level expression.
- An unexpected trailing token.
- A lexer `tok_error`.

This is **panic-mode recovery**. The parser does not guess what the user meant; it advances to a reliable synchronization point and starts fresh on the next line.

The boundary is:

```text
one bad line -> one diagnostic -> next line still parses
```

## 11. Print the Prompt in One Place

Print the first prompt in `main()`, then print another only when `MainLoop()` consumes a newline. Do not print prompts independently in every handler.

```cpp
int main() {
  fprintf(stderr, "ready> ");
  getNextToken();
  MainLoop();
  return 0;
}
```

This avoids missing and doubled prompts after success or recovery.

## 12. Build and Run

```bash
cmake -S . -B build
cmake --build build
./build/pyxc
```

Try a missing colon:

```pyxc
ready> def add(a, b)
```

Expected shape:

```text
Error (Line 1, Column 14): Expected ':' in function definition
def add(a, b)
             ^~~~
```

Try a malformed number followed by valid input:

```pyxc
ready> 1.2.3
ready> 4 + 5
```

Expected behavior:

```text
invalid number literal '1.2.3'
Parsed a top-level expression.
```

Run the suite:

```bash
llvm-lit -v test/
```

What you built is a reusable diagnostic pipeline:

```text
buffer line -> track token start -> format error -> print caret -> recover
```

Next: [Chapter 6](chapter-06.md) installs LLVM and verifies the compiler can discover it.

## Need Help?

Build issues? Questions?

- [Report a problem with GitHub Issues](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- [Ask a question in GitHub Discussions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:

- Your operating system and version
- The chapter number
- The exact command you ran
- The complete error message
- The output of `c++ --version` and `cmake --version`
- The output of `llvm-config --version` for Chapter 6 and later
