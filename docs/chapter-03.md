---
description: "Polish the lexer and add proper diagnostics: a keyword map, malformed-number detection, source locations, and caret-style error messages."
---
# 3. Pyxc: Better Errors

## Where We Are

The parser from [Chapter 2](chapter-02.md) works, but its error messages are rough. 

```python
ready> def bad(x) return x
Error: Expected ':' in function definition (token: -7)
```

By the end of this chapter the same mistake gives you:

```
Error (Line 1, Column 12): Expected ':' in function definition
def bad(x) return 
           ^~~~
```

Line number. Column number. The source line. A caret pointing at the problem. That's a real error message. The `(token: -4)` is a raw enum value. It means nothing to someone who didn't write the lexer. 

And if you mistype a number —

```python
ready> 1.2.3
Parsed a top-level expression.
```

That's a bug: `1.2.3` isn't a valid number, but the lexer silently accepted `1.2` and left `.3` sitting in the stream. By the end of this chapter the same mistake gives you:

```
Error (Line 2, Column 1): invalid number literal '1.2.3'
1.2.3
^~~~
```


## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-03
```

## The Four Problems

Everything in this chapter addresses a concrete deficiency from Chapter 2:

1. **Keyword recognition is a chain of `if` comparisons.** Adding a new keyword means editing the comparison chain. A table is cleaner.
2. **`strtod` can silently accept junk.** `strtod("1.2.3", nullptr)` returns `1.2` and ignores `.3`. We need to detect the leftover.
3. **Error messages show raw token numbers.** We need a human-readable name for every token.
4. **Error messages have no location.** We need to track line and column as we read characters, and attach those coordinates to each error.

## Problem 1: A Table for Keywords

Chapter 1's keyword check looks like this:

```cpp
if (IdentifierStr == "def")    return tok_def;
if (IdentifierStr == "extern") return tok_extern;
if (IdentifierStr == "return") return tok_return;
return tok_identifier;
```

This works, but every new keyword needs a new `if`. A map is more honest about what's happening — it *is* a lookup table — and adding a keyword is a one-line change:

```cpp
static map<string, Token> Keywords = {
    {"def", tok_def}, {"extern", tok_extern}, {"return", tok_return}};
```

The lookup replaces the chain:

```cpp
auto It = Keywords.find(IdentifierStr);
return (It == Keywords.end()) ? tok_identifier : It->second;
```

Not found → it's an identifier. Found → return the mapped token. Same behavior, open for extension.

## Problem 2: Catching Malformed Numbers

The standard library function `strtod` converts a string to a `double`. It stops at the first character it doesn't recognize and tells you where it stopped via a second argument:

```cpp
char *End = nullptr;
NumVal = strtod(NumStr.c_str(), &End);
```

After the call, `End` points to the first character `strtod` didn't consume. If `End` points to the null terminator (`*End == '\0'`), the entire string was valid. If it points anywhere else, there's unconsumed text — which means the input was malformed.

```cpp
if (!End || *End != '\0') {
    fprintf(stderr,
            "Error (Line %d, Column %d): invalid number literal '%s'\n",
            CurLoc.Line, CurLoc.Col, NumStr.c_str());
    PrintErrorSourceContext(CurLoc);
    return tok_error;
}
```

`1.2.3` produces `NumStr = "1.2.3"`. `strtod` stops at the second `.`, leaving `End` pointing at `.3`. Since `*End != '\0'`, we emit an error and return `tok_error` — a new token value that signals "the lexer already diagnosed this, skip it."

We also save the literal string before calling `strtod`:

```cpp
NumLiteralStr = NumStr;
```

`NumLiteralStr` is used by `FormatTokenForMessage` later when a parse error involves a number token. The lexer sets it; nobody else needs to care about it.

## Problem 3: A Name for Every Token

Chapter 2's error messages printed the raw integer value of `CurTok` — helpful only if you have the `Token` enum open in another window. We want something like `'def'`, `identifier`, or `newline` instead.

We build a map from token value to string once, at startup, using an immediately-invoked lambda:

```cpp
static map<int, string> TokenNames = [] {
  map<int, string> Names = {
      {tok_eof,        "end of input"},
      {tok_eol,        "newline"},
      {tok_error,      "error"},
      {tok_def,        "'def'"},
      {tok_extern,     "'extern'"},
      {tok_identifier, "identifier"},
      {tok_number,     "number"},
      {tok_return,     "'return'"},
  };

  // Single character tokens.
  for (int ch = 0; ch <= 255; ++ch) {
    if (isprint(static_cast<unsigned char>(ch)))
      Names[ch] = "'" + string(1, static_cast<char>(ch)) + "'";
    else if (ch == '\n')
      Names[ch] = "'\\n'";
    else if (ch == '\t')
      Names[ch] = "'\\t'";
    else if (ch == '\r')
      Names[ch] = "'\\r'";
    else if (ch == '\0')
      Names[ch] = "'\\0'";
    else {
      ostringstream OS;
      OS << "0x" << uppercase << hex << setw(2) << setfill('0') << ch;
      Names[ch] = OS.str();
    }
  }

  return Names;
}();
```

The named token values (negative integers) are in the initializer list. Every printable ASCII character gets a quoted name like `'+'`. Unprintable characters get either an escape sequence or a hex code. The lambda runs once and the result is stored. No runtime cost after startup.

`FormatTokenForMessage` uses this map, with special cases for the tokens that carry extra information:

```cpp
static string FormatTokenForMessage(int Tok) {
  if (Tok == tok_identifier)
    return "identifier '" + IdentifierStr + "'";
  if (Tok == tok_number)
    return "number '" + NumLiteralStr + "'";

  auto It = TokenNames.find(Tok);
  if (It != TokenNames.end())
    return It->second;
  return "unknown token";
}
```

When the bad token is an identifier or a number, we include the actual text (`identifier 'foo'`, `number '3.14'`). Everything else uses the static name from the map.

## Problem 4: Tracking Where We Are

To report `(Line 3, Column 8)`, we need to know the line and column as we read characters. We introduce two small pieces of data.

### Tracking Position Through advance()

In Chapter 1, the lexer called `getchar()` directly. We introduce `advance()` as a wrapper that keeps a running position:

```cpp
struct SourceLocation {
  int Line;
  int Col;
};
static SourceLocation CurLoc;
static SourceLocation LexLoc = {1, 0};
```

Two location globals: `LexLoc` is where the lexer's character-read head currently sits. `CurLoc` is snapshotted at the start of each token — the position the *parser* sees.

`advance()` updates `LexLoc` every time a character is consumed:

```cpp
static int advance() {
  int LastChar = getchar();
  if (LastChar == '\r') {
    int NextChar = getchar();
    if (NextChar != '\n' && NextChar != EOF)
      ungetc(NextChar, stdin);
    PyxcSourceMgr.onChar('\n');
    LexLoc.Line++;
    LexLoc.Col = 0;
    return '\n';
  }

  if (LastChar == '\n') {
    PyxcSourceMgr.onChar('\n');
    LexLoc.Line++;
    LexLoc.Col = 0;
  } else {
    PyxcSourceMgr.onChar(LastChar);
    LexLoc.Col++;
  }

  return LastChar;
}
```

Two things happen here. First, `\r\n` (Windows line endings) is coalesced into a single `\n` — the rest of the lexer never needs to handle `\r`. Second, `LexLoc` is updated: a newline increments the line counter and resets the column to zero; any other character increments the column.

`gettok()` snapshots `LexLoc` into `CurLoc` once, after the whitespace-skip loop but before any token branch:

```cpp
while (isspace(LastChar) && LastChar != '\n')
  LastChar = advance();

CurLoc = LexLoc;
```

This is the position the diagnostics infrastructure uses. Snapshotting here — after skipping whitespace, before consuming the token's characters — means `CurLoc` always points to the first character of the current token.

For `tok_eol`, `LastChar` is `'\n'` which was already consumed by `advance()` in a previous call. At that point `LexLoc` has already advanced to the next line (line counter incremented, column reset to 0). We snapshot `CurLoc = LexLoc` *before* the `'\n'` check, so `CurLoc.Line` is the new (next) line number. `GetDiagnosticAnchorLoc` steps back by one to arrive at the line that just ended:

```cpp
if (LastChar == '\n') {
  LastChar = ' ';
  return tok_eol;
}
```

There is one edge case: when `gettok` returns `tok_eol` from the comment path (`#` branch), the snapshot at the top of the function pointed at `#`, not at the newline. We re-snapshot just before returning to get the correct post-newline position:

```cpp
if (LastChar == '#') {
  do
    LastChar = advance();
  while (LastChar != EOF && LastChar != '\n');

  if (LastChar != EOF) {
    CurLoc = LexLoc;  // re-snapshot after consuming the whole comment + '\n'
    LastChar = ' ';
    return tok_eol;
  }
}
```

### Buffering Source Lines for Caret Output

Knowing the position isn't enough on its own. To print:

```
def bad(x) return 
           ^~~~
```

we need the actual text of the line. But by the time an error is reported, we may be partway through a different line — the `getchar()` calls are long past the error line.

We solve this by buffering lines as we read. `SourceManager` accumulates characters through `onChar()`, which gets called by `advance()` on every character consumed:

```cpp
class SourceManager {
  vector<string> CompletedLines;
  string CurrentLine;

public:
  void reset() {
    CompletedLines.clear();
    CurrentLine.clear();
  }

  void onChar(int C) {
    if (C == '\n') {
      CompletedLines.push_back(CurrentLine);
      CurrentLine.clear();
      return;
    }
    if (C != EOF)
      CurrentLine.push_back(static_cast<char>(C));
  }

  const string *getLine(int OneBasedLine) const {
    if (OneBasedLine <= 0)
      return nullptr;
    size_t Index = static_cast<size_t>(OneBasedLine - 1);
    if (Index < CompletedLines.size())
      return &CompletedLines[Index];
    if (Index == CompletedLines.size())
      return &CurrentLine;
    return nullptr;
  }
};

static SourceManager PyxcSourceMgr;
```

When a newline arrives, the completed line is appended to `CompletedLines` and `CurrentLine` is reset. `getLine(N)` returns the Nth line (1-based): completed lines come from `CompletedLines`; the line currently being assembled comes from `CurrentLine`.

### Printing the Caret

With a stored line and a column number, printing the context is straightforward:

```cpp
static void PrintErrorSourceContext(SourceLocation Loc) {
  const string *LineText = PyxcSourceMgr.getLine(Loc.Line);
  if (!LineText)
    return;

  fprintf(stderr, "%s\n", LineText->c_str());
  int spaces = Loc.Col - 1;
  if (spaces < 0)
    spaces = 0;
  for (int i = 0; i < spaces; ++i)
    fputc(' ', stderr);
  fprintf(stderr, "^~~~\n");
}
```

Print the line, then print `(Col - 1)` spaces, then `^~~~`. The `-1` converts from 1-based column to a 0-based offset into the string.

### Pointing at the Right Place for tok_eol

When the parser fails on a newline token — for example, when the user types `def foo(x)` and hits Enter without a `:` — the error is logically at the end of the previous line, not at the start of the next one.

Because `CurLoc` for `tok_eol` is snapshotted *after* `advance()` has consumed the `\n` and incremented `LexLoc.Line`, `CurLoc.Line` is already the *next* line number. `GetDiagnosticAnchorLoc` steps back by one (`Loc.Line - 1`) to arrive at the line that just ended, then reports a column one past its last character so the caret appears just after the final token:

```cpp
static SourceLocation GetDiagnosticAnchorLoc(SourceLocation Loc, int Tok) {
  if (Tok != tok_eol)
    return Loc;

  int PrevLine = Loc.Line - 1;
  if (PrevLine <= 0)
    return Loc;

  const string *PrevLineText = PyxcSourceMgr.getLine(PrevLine);
  if (!PrevLineText)
    return Loc;

  return {PrevLine, static_cast<int>(PrevLineText->size()) + 1};
}
```

For any other token, `CurLoc` is returned as-is.

## Putting It Together: LogError

The three `LogError` overloads now use the location infrastructure:

```cpp
unique_ptr<ExprAST> LogError(const char *Str) {
  SourceLocation Anchor = GetDiagnosticAnchorLoc(CurLoc, CurTok);
  fprintf(stderr, "Error (Line %d, Column %d): %s\n",
          Anchor.Line, Anchor.Col, Str);
  PrintErrorSourceContext(Anchor);
  return nullptr;
}

unique_ptr<PrototypeAST> LogErrorP(const char *Str) {
  LogError(Str);
  return nullptr;
}

unique_ptr<FunctionAST> LogErrorF(const char *Str) {
  LogError(Str);
  return nullptr;
}
```

Every parser error now shows:
- The location of the bad token (or end of line, for `tok_eol`)
- The source line
- A `^~~~` caret

The three overloads exist for the same reason as before: `LogError` returns `nullptr` as `unique_ptr<ExprAST>`, `LogErrorP` as `unique_ptr<PrototypeAST>`, `LogErrorF` as `unique_ptr<FunctionAST>`. C++ can't overload on return type.

## Error Recovery: tok_error and SynchronizeToLineBoundary

The lexer now returns `tok_error` for malformed input (like `1.2.3`). The rest of the lexer has no idea how to handle that token — it's not a number, not an operator, not a keyword. If we let it fall through to `ParsePrimary`, it hits the `default:` branch and emits a second, confusing error: `"unknown token when expecting an expression"` — on top of the error the lexer already printed.

The fix is to intercept `tok_error` early and skip to the next line before trying to parse anything:

```cpp
static void SynchronizeToLineBoundary() {
  while (CurTok != tok_eol && CurTok != tok_eof)
    getNextToken();
}
```

This is **panic-mode error recovery**: when something goes wrong and we can't reason about the current state, advance unconditionally to the next line boundary and restart parsing there. It's a blunt instrument — we discard the rest of the line — but it's reliable: after `SynchronizeToLineBoundary()`, `CurTok` is always `tok_eol` or `tok_eof`, and the REPL's main loop knows exactly how to handle those.

`MainLoop` calls it for `tok_error`:

```cpp
if (CurTok == tok_error) {
  SynchronizeToLineBoundary();
  continue;
}
```

The Handle functions also call it on parse failure and on unexpected trailing tokens:

```cpp
static void HandleDefinition() {
  if (ParseDefinition()) {
    if (CurTok != tok_eol && CurTok != tok_eof) {
      LogError(("Unexpected " + FormatTokenForMessage(CurTok)).c_str());
      SynchronizeToLineBoundary();
      return;
    }
    fprintf(stderr, "Parsed a function definition.\n");
  } else {
    SynchronizeToLineBoundary();
  }
}
```

The same pattern applies to `HandleExtern` and `HandleTopLevelExpression`. After any failure — whether the parser returned `nullptr` or left unexpected tokens in `CurTok` — we synchronize to the line boundary and let the main loop print a fresh prompt.

## Build and Run

```bash
cd code/chapter-03
cmake -S . -B build && cmake --build build
./build/pyxc
```

## Try It

```python
ready> def add(x, y):
   return x + y
Parsed a function definition.
ready> 1.2.3
Error (Line 3, Column 1): invalid number literal '1.2.3'
1.2.3
^~~~
ready> def bad(x) return x
Error (Line 4, Column 12): Expected ':' in function definition
def bad(x) return 
           ^~~~
ready> def missing_colon(x)
Error (Line 5, Column 21): Expected ':' in function definition
def missing_colon(x)
                    ^~~~
ready>^D
```

A few things to notice:

- `1.2.3` is caught in the lexer now. The error fires before the parser ever sees the token.
- `def bad(x) return x` — the caret points at the space before `return`, the position where `:` was expected.
- `def missing_colon(x)` — the caret points just past the closing `)`, where `:` should have appeared. That's `GetDiagnosticAnchorLoc` at work: `CurLoc` for `tok_eol` is on the next line, so the function steps back by one and points to the end of the line that just ended.

## What We Built

| Piece | What it does |
|---|---|
| `Keywords` map | Replaces the keyword `if`-chain; adding a keyword is a one-liner |
| `NumLiteralStr` | Saves the original number text for error messages |
| `strtod` + `*End` check | Catches malformed literals like `1.2.3`; returns `tok_error` |
| `tok_error` | Signals "lexer already diagnosed this" so the parser doesn't double-report |
| `TokenNames` map | Human-readable name for every token value |
| `FormatTokenForMessage` | Adds the actual text for identifier and number tokens |
| `SourceLocation` | A `{Line, Col}` pair attached to each token |
| `LexLoc` / `CurLoc` | Two locations: where the character reader is, and where the current token started |
| `advance()` | Single character-read point; updates `LexLoc` and feeds `SourceManager` |
| `SourceManager` | Buffers source lines as they are read so they can be reprinted in errors |
| `PrintErrorSourceContext` | Prints the source line and a `^~~~` caret |
| `GetDiagnosticAnchorLoc` | Remaps `tok_eol` errors to the end of the previous line |
| `LogError` / `LogErrorP` / `LogErrorF` | Now print `(Line N, Column N):` and source context |
| `SynchronizeToLineBoundary` | Panic-mode recovery: skip to `tok_eol`/`tok_eof` after any error |

## What's Next

The lexer and parser are solid. Error messages are readable. The next step is to connect this to LLVM: walk the AST and emit LLVM IR — real machine-code instructions — for the first time.

Before that, Chapter 4 covers installing LLVM and setting up the build system. It's mostly infrastructure, but you only do it once.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version` and `ninja --version`

We'll figure it out.
