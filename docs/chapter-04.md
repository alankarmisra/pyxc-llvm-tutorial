---
description: "Polish the lexer and add proper diagnostics: a keyword map, malformed-number detection, source locations, and caret-style error messages."
---
# 4. pyxc: Better Errors

## Where We Are

I have a nice little parser after [Chapter 2](chapter-02.md). But the error messages are kinda rough. As I grow the language, and type code in my invented syntax, better error messaging will really help me narrow down whether something is a code syntax problem or a compiler problem. I take compiler correctness for granted when I use production-level languages. But in inventing my own, I have to be wary of the fact that my compiler might be doing things wrong. Better error messages go a long way in tracing the problem. So I tackle this first, before moving on to generating machine code from source in the following chapters. 

I'm going to attempt to make this:
```pyxc
ready> def bad(x) return x
Error: Expected ':' in function definition (token: -6)
```

look like this:

```
Error (Line 1, Column 12): Expected ':' in function definition
def bad(x) return 
           ^~~~
```

Line number. Column number. The source line. A caret pointing at the problem. That's a real error message. 

And then there's this bug I found in the previous chapter:

```pyxc
ready> 1.2.3
Parsed a top-level expression.
```

Internally this is accepted as `1.2` and the *.3* is carelessly ignored. I'll fix this too so I get the following:

```
Error (Line 2, Column 1): invalid number literal '1.2.3'
1.2.3
^~~~
```

That's it. Just two fixes and I'll be well on my way to making this code execute in the next few chapters. Getting there. Don't give up on me now.  

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-04
```

## A Name for Every Token

To print sensible error strings, I want to convert `Token` values to strings like `def`, `identifier`, or `newline`. A `map` serves my purpose well. But I also want to generate some strings through a code loop, so I wrap the whole thing in a lambda and execute it immediately.

```cpp
static map<int, string> TokenNames = [] {
  static map<int, string> Names = {
      {tok_eof,        "end of input"},
      {tok_eol,        "newline"},
      {tok_error,      "error"},
      {tok_def,        "'def'"},
      {tok_identifier, "identifier"},
      {tok_number,     "number"},
      {tok_return,     "'return'"},
  };

  // Single character tokens.
  for (int ch = 0; ch <= 255; ++ch) {
    if (isprint(static_cast<unsigned char>(ch))) // isprint expects unsigned char; int ch can be negative on some platforms
      Names[ch] = "'" + string(1, static_cast<char>(ch)) + "'"; // string(n, char): needs char, not int
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

The named token values (negative integers) are in the initializer list. Every printable ASCII character gets a quoted name like `'+'`. Unprintable characters get either an escape sequence or a hex code. The lambda runs once and the result is stored.

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

When the bad token is an identifier or a number, I include the actual text (`identifier 'foo'`, `number '3.14'`). Everything else uses the static name from the map.

## Tracking Where I Am

To report `(Line 3, Column 8)`, I need to know the line and column as I read characters. I introduce two small pieces of data.

```cpp
struct SourceLocation {
  int Line;
  int Col;
};
static SourceLocation CurLoc;
static SourceLocation LexLoc = {1, 0};
```

Two location globals: `LexLoc` is where the lexer's character-read head currently sits. `CurLoc` is snapshotted at the start of each token — the position the *parser* sees. 


In Chapter 1, `advance()` already wrapped `getchar()` to normalize line endings. Here I expand it to also keep a running position:
```cpp
static int advance() {
  int LastChar = getchar();
  if (LastChar == '\r') {
    int NextChar = getchar();
    if (NextChar != '\n' && NextChar != EOF)
      ungetc(NextChar, stdin);
    LexLoc.Line++;
    LexLoc.Col = 0;
    return '\n';
  }

  if (LastChar == '\n') {
    LexLoc.Line++;
    LexLoc.Col = 0;
  } else {
    LexLoc.Col++;
  }

  return LastChar;
}
```

A newline increments the `LexLoc` line counter and resets the column to zero; any other character increments the column.

`gettok()` snapshots `LexLoc` into `CurLoc` once, after the whitespace-skip loop:

```cpp
while (isspace(LastChar) && LastChar != '\n')
  LastChar = advance();

CurLoc = LexLoc;
```

This is the position that will be printed, should an error occur. Snapshotting here — after skipping whitespace, before consuming the token's characters — means `CurLoc` always points to the first character of the current token.

There is one edge case: when `gettok` returns `tok_eol` from the comment path (`#` branch), the snapshot at the top of the function pointed at `#`, not at the newline. So I re-snapshot just before returning from the function, to get the correct post-newline position:

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

I need the actual text of the line. I solve this by buffering lines as I read. `SourceManager` accumulates characters through `onChar()`, which gets called by `advance()` on every character consumed:

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

Characters accumulate in `CurrentLine` as they are read. When a newline arrives, `CurrentLine` is moved into `CompletedLines` and the `CurrentLine` buffer is reset. `getLine(N)` takes a 1-based line number and returns from `CompletedLines` for finished lines, or from `CurrentLine` for the line still being read.

I integrate `SourceManager` into `advance()`:

```cpp
  if (LastChar == '\r') {
    ...
    PyxcSourceMgr.onChar('\n'); // add this
    LexLoc.Line++;
    ...
  }

  if (LastChar == '\n') {
    PyxcSourceMgr.onChar('\n'); // add this
    LexLoc.Line++;
    LexLoc.Col = 0;
  } else {
    PyxcSourceMgr.onChar(LastChar); // add this
    LexLoc.Col++;
  }
```

Every character consumed by the lexer passes through `onChar` before being returned. `SourceManager` sees the whole character stream and builds its line buffer passively — no other part of the lexer needs to know about it.

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

For `def foo(x)` followed by Enter, this produces:

```
Error (Line 1, Column 11): Expected ':' in function definition
def foo(x)
          ^~~~
```

The caret lands just past the `)` — exactly where the `:` was missing.

## Putting It Together: LogError

`LogError` overloads now use the location infrastructure:

```cpp
unique_ptr<ExprAST> LogError(const char *Str) {
  SourceLocation Anchor = GetDiagnosticAnchorLoc(CurLoc, CurTok);
  fprintf(stderr, "Error (Line %d, Column %d): %s\n",
          Anchor.Line, Anchor.Col, Str);
  PrintErrorSourceContext(Anchor);
  return nullptr;
}
```

Since `LogErrorP` and `LogErrorF` delegate to `LogError`, they get this for free.

Every parser error now shows:
- The location of the bad token (or end of line, for `tok_eol`)
- The source line
- A `^~~~` caret

## Error Recovery: tok_error and SynchronizeToLineBoundary

The lexer now returns `tok_error` for funky input (like `1.2.3`). The rest of the lexer has no idea how to handle that token — it's not a number, not an operator, not a keyword. If I let it fall through to `ParsePrimary`, it hits the `default:` branch and emits a second, confusing error: `"unknown token when expecting an expression"` — on top of the error the lexer already printed.

The fix is to intercept `tok_error` early and skip to the next line before trying to parse anything:

```cpp
static void SynchronizeToLineBoundary() {
  while (CurTok != tok_eol && CurTok != tok_eof)
    getNextToken();
}
```

This is **panic-mode error recovery**: when something goes wrong and I can't reason about the current state, advance unconditionally to the next line boundary and restart parsing there. It's a blunt instrument — I discard the rest of the line — but it's reliable: after `SynchronizeToLineBoundary()`, `CurTok` is always `tok_eol` or `tok_eof`, and the REPL's main loop knows exactly how to handle those.

`MainLoop` calls it for `tok_error`:

```cpp
if (CurTok == tok_error) {
  SynchronizeToLineBoundary();
  continue;
}
```

The Handle* functions also call it on parse failure and on unexpected trailing tokens:

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

The same pattern applies to `HandleTopLevelExpression`. After any failure — whether the parser returned `nullptr` or left unexpected tokens in `CurTok` — I synchronize to the line boundary and let the main loop print a fresh prompt.

## Catching Malformed Numbers

Let's deal with malformed numbers now. It's a really quick fix. The standard library function `strtod` converts a string to a `double`. It stops at the first character it doesn't recognize and tells you where it stopped via a second argument:

```cpp
char *End = nullptr;
NumVal = strtod(NumStr.c_str(), &End);
```

After the call, `End` points to the first character `strtod` didn't consume. If `End` points to the null terminator (`*End == '\0'`), the entire string was valid. If it points anywhere else, there's unconsumed text left over — which means the input was *not* a valid number.

I already have `PrintErrorSourceContext` from the caret work above, so reporting this is just a small helper wrapping it:

```cpp
static void LogInvalidNumberLiteralAtLoc(const string &Literal, SourceLocation Loc) {
  fprintf(stderr, "Error (Line %d, Column %d): invalid number literal '%s'\n",
          Loc.Line, Loc.Col, Literal.c_str());
  PrintErrorSourceContext(Loc);
}
```

`gettok()` is defined earlier in the file than this helper, so I need a forward declaration next to `SourceManager` — the same thing I already had to do for `PrintErrorSourceContext`. With that in place, the number-parsing branch becomes:

```cpp
if (!End || *End != '\0') {
  LogInvalidNumberLiteralAtLoc(NumStr, CurLoc);
  return tok_error;
}
```

`1.2.3` produces `NumStr = "1.2.3"`. `strtod` stops at the second `.`, leaving `End` pointing at `.3`. Since `*End != '\0'`, I call the helper and return `tok_error` — a new token value that signals "the lexer already diagnosed this, skip it."

I also save the literal string before calling `strtod`:

```cpp
NumLiteralStr = NumStr;
```

`NumLiteralStr` is used by `FormatTokenForMessage` later when a parse error involves a number token. The lexer sets it; nobody else needs to care about it.

Notice I'm in the lexer section of the code in `gettok()`, which returns an `int`. So I can't return `LogError(...)` here like I do with parser-level errors, which returns `nullptr`. For now, I just call the helper directly from inside `gettok()` and move on. If I find myself printing more and more lexer errors inline, I'll refactor it. 

## Build and Run

```bash
cd code/chapter-04
cmake -S . -B build && cmake --build build
./build/pyxc
```

## Tests

```bash
llvm-lit code/chapter-04/test/
```

The test suite covers the error cases introduced in this chapter — malformed numbers, missing colons, bad separators — as well as location accuracy across sequential lines, comments, and recovery after an error. Peek into `code/chapter-04/test/` for examples.

## Try It

```pyxc
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

## What's Next

The lexer and parser are solid. Error messages are readable. The next step is to connect this to LLVM: walk the AST and emit LLVM IR — real machine-code instructions — for the first time.

Before that, [Chapter 5](chapter-05.md) covers installing LLVM and setting up the build system. It's mostly infrastructure, but you only do it once.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version` and `ninja --version`

We'll figure it out.
