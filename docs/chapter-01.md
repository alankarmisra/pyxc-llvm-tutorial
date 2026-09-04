---
section: "Foundations"
description: "Build the first pyxc lexer and turn source characters into tokens."
---

# 1. pyxc: Analyzing Program Words

Start with the smallest compiler boundary:

```text
characters -> tokens
```

Given:

```pyxc
# add.pyxc
def add(a, b): a + b
```

the first version of pyxc should print:

```text
newline
'def'
name: add
'('
name: a
','
name: b
')'
':'
name: a
'+'
name: b
newline
```

This component is the **lexer**. It does not decide whether the program is grammatically valid. It only groups characters into meaningful units — a process called *lexing*, from the Greek *lexis*, "word."

Work in:

```bash
cd code/chapter-01
```

## 1. Create the Token Vocabulary

Add the headers and namespace used by the lexer:

```cpp
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

using namespace std;
```

Then define the tokens this chapter understands:

```cpp
enum Token {
  tok_eof = 1,
  tok_eol,
  tok_error,

  tok_def,
  tok_name,
  tok_number,

  tok_lparen,
  tok_rparen,
  tok_comma,
  tok_colon,
  tok_plus,
};
```

Use named tokens instead of raw characters: pass `"def"`, `"add"`, `"("` around and every later stage has to re-compare strings to recognize them. Pass `tok_def` around instead, and recognizing it is one integer comparison, and diagnostics can print readable names from it.

Some tokens carry data. `def`, `(`, `+` are always the same characters, but a name or number is different every time, so one enum value can't represent it. Add two side channels for the token most recently read:

```cpp
static string Name;
static double NumberValue;
```

The lexer returns `tok_name` or `tok_number`; these variables hold the corresponding spelling or numeric value. One `Name` variable is enough for the whole lexer, because it's always read immediately after the `tok_name` that set it, before the next name is lexed. That stops being true once names need to outlive a single `getToken()` call — the parser will copy `Name` into longer-lived AST nodes starting next chapter.

## 2. Separate Keywords from Names

Add the first keyword table:

```cpp
static map<string, Token> Keywords = {
    {"def", tok_def},
};
```

Both `def` and `add` begin as the same character pattern: a letter followed by letters, digits, or underscores. Read the full word first, then use this table to decide whether it is reserved.

Add readable token names for the driver:

```cpp
static map<int, string> TokenNames = {
    {tok_eof, "end of input"}, {tok_eol, "newline"},
    {tok_error, "error"},      {tok_def, "'def'"},
    {tok_name, "name"},        {tok_number, "number"},
    {tok_lparen, "'('"},       {tok_rparen, "')'"},
    {tok_comma, "','"},        {tok_colon, "':'"},
    {tok_plus, "'+'"},
};
```

## 3. Normalize Newlines at the Input Boundary

Source files may contain `\n`, `\r\n`, or bare `\r`. Make all three look like `\n` to the lexer:

```cpp
int advance() {
  int LastChar = getchar();

  if (LastChar == '\r') {
    int NextChar = getchar();
    if (NextChar != '\n' && NextChar != EOF)
      ungetc(NextChar, stdin);
    return '\n';
  }

  return LastChar;
}
```

This creates one invariant for the rest of the compiler:

```text
every source line ending -> '\n'
```

## 4. Keep One Character of Lookahead

Start `getToken()` with:

```cpp
int getToken() {
  static int LastChar = ' ';

  while (isspace(LastChar) && LastChar != '\n')
    LastChar = advance();
```

`LastChar` is the next character not yet assigned to a token. It survives between calls, giving the lexer one character of lookahead without a separate buffer. Seeding it with a space (not a character from the file) makes the loop above skip leading whitespace correctly on the very first call too.

Skip horizontal whitespace, but do not skip newlines. Newlines will become tokens because the parser will eventually use them to separate top-level forms.

## 5. Read Names and Keywords

Add the first token branch:

```cpp
if (isalpha(LastChar) || LastChar == '_') {
  string NameLiteral;
  NameLiteral = LastChar;

  while (isalnum(LastChar = advance()) || LastChar == '_')
    NameLiteral += LastChar;

  auto KeywordIt = Keywords.find(NameLiteral);
  if (KeywordIt != Keywords.end())
    return KeywordIt->second;

  Name = NameLiteral;
  return tok_name;
}
```

When the loop stops, `LastChar` already contains the first character belonging to the next token. Leave it there for the next `getToken()` call.

## 6. Read Numbers

Add numeric literals next:

```cpp
if (isdigit(LastChar) || LastChar == '.') {
  string NumberLiteral;

  do {
    NumberLiteral += LastChar;
    LastChar = advance();
  } while (isdigit(LastChar) || LastChar == '.');

  NumberValue = strtod(NumberLiteral.c_str(), 0);
  return tok_number;
}
```

This intentionally accepts a broad sequence of digits and dots — anything matching `[0-9.]+` — and hands the whole thing to [`strtod`](https://en.cppreference.com/w/cpp/string/byte/strtof), which parses as much of a valid number as it can from the front and stops.

That's fine for `42`, `3.14`, and `.5`. It's not fine for something like `1.23.45.67`: the loop above still consumes every character of it into `NumberLiteral`, but `strtod` only converts the `1.23` prefix and silently ignores the rest. The `.45.67` doesn't become a token and doesn't raise an error — it has already been consumed from the input stream by the time `strtod` gives up on it, so it just disappears. Chapter 5 adds real validation to reject input like this outright; for now, this is a known bug, not a corner case worth chasing here.

## 7. Discard Comments but Preserve Their Newline

Add:

```cpp
if (LastChar == '#') {
  do {
    LastChar = advance();
  } while (LastChar != '\n' && LastChar != EOF);

  if (LastChar == '\n') {
    LastChar = advance();
    return tok_eol;
  }
}
```

The comment text disappears, but the line boundary remains — a comment-only line still produces a `tok_eol`, the same as a blank line would.

## 8. Recognize Newline and EOF

Add these checks after comment handling:

```cpp
if (LastChar == '\n') {
  LastChar = advance();
  return tok_eol;
}

if (LastChar == EOF)
  return tok_eof;
```

`tok_eol` separates forms. `tok_eof` tells the driver to stop asking for tokens.

## 9. Recognize One-Character Tokens

Finish `getToken()` with:

```cpp
int ThisChar = LastChar;
LastChar = advance();

switch (ThisChar) {
case '(':
  return tok_lparen;
case ')':
  return tok_rparen;
case ',':
  return tok_comma;
case ':':
  return tok_colon;
case '+':
  return tok_plus;
default:
  return tok_error;
}
}
```

Consume the character before returning. Otherwise the next call would report the same token forever.

## 10. Add a Token-Printing Driver

Use the lexer until it returns `tok_eof`:

```cpp
int main() {
  int Token;

  while ((Token = getToken()) != tok_eof) {
    if (Token == tok_name)
      fprintf(stdout, "%s: %s\n", TokenNames.at(Token).c_str(),
              Name.c_str());
    else if (Token == tok_number)
      fprintf(stdout, "%s: %g\n", TokenNames.at(Token).c_str(),
              NumberValue);
    else
      fprintf(stdout, "%s\n", TokenNames.at(Token).c_str());
  }

  return 0;
}
```

`tok_name` and `tok_number` print their attached value; every other token prints its own name, since the token itself is the whole story. This driver is temporary — Chapter 2 replaces it with a parser — but it gives the lexer a clean, observable output today.

## 11. Build and Run

Configure and compile:

```bash
cmake -S . -B build
cmake --build build
```

Run the sample:

```bash
printf 'def add(a, b): a + b\n' | ./build/pyxc
```

Expected:

```text
'def'
name: add
'('
name: a
','
name: b
')'
':'
name: a
'+'
name: b
newline
```

Try a comment and decimal:

```bash
printf '# value\n12.5\n' | ./build/pyxc
```

Expected:

```text
newline
number: 12.5
newline
```

Now trigger the number-parsing bug from step 6 directly:

```bash
printf '1.23.45.67\n' | ./build/pyxc
```

Expected:

```text
number: 1.23
newline
```

`.45.67` never appears — not as a second number, not as an error. That's the bug: it was already consumed off the input stream before `strtod` got a chance to reject it.

## 12. Run the Tests

Use LLVM's lightweight test runner if it is already available:

```bash
llvm-lit -v test/
```

The chapter tests cover names, underscores, integers, decimals, comments, punctuation, unknown characters, token boundaries, and newline normalization. If the `# RUN:`/`# CHECK:` lines inside those test files don't make sense yet, [Appendix 1](appendix-01.md) walks through exactly how `lit` reads and runs them.

A clean `llvm-lit` run only means those specific tests pass, not that everything worth testing has been tested. [Appendix 2](appendix-02.md) covers checking that more rigorously with LLVM's own coverage tooling.

What you built is intentionally narrow:

```text
one call to getToken() -> one token and optional payload
```

Next: [Chapter 2](chapter-02.md) consumes those tokens and builds a syntax tree.
