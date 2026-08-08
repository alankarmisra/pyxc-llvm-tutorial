---
description: "Give every token a readable name, track source locations through the lexer, and print caret-style diagnostics instead of a bare token description."
---
# 4. pyxc: Better Errors

## Where We Are

Right now, a missing `:` looks like this:

```pyxc
ready> def bad(x) return x
Error: Expected ':' in function definition (token: name)
```

`(token: name)` doesn't tell me which name, or where. I want this instead:

```
ready> def bad(x) return x
Error (Line 1, Column 12): Expected ':' in function definition
def bad(x) return 
           ^~~~
```

Line. Column. The actual source line. A caret pointing at the exact spot. There's also a second bug I've been ignoring: `1.2.3` currently parses without complaint.

```pyxc
ready> 1.2.3
Parsed a top-level expression.
```

`strtod` reads as much of `"1.2.3"` as looks like a number (`1.2`) and silently ignores the rest. I fix both problems in this chapter.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-04
```

## Naming Every Token

To say what went wrong, I first need to say what I *saw*. Up to now, `TokenNames` has been a hand-written map, one entry per token I bothered to declare: `tok_lparen` to `"'('"`, `tok_plus` to `"'+'"`, and so on. That only covers the characters I thought to list. If the lexer ever hits a byte I didn't anticipate, I have no name for it.

Instead of listing tokens by hand, I generate a name for every possible byte value once, at startup:

```cpp
static map<int, string> TokenNames = [] {
  // Unprintable character tokens, and multi-character tokens.
  static map<int, string> Names = {
      {tok_eof, "end of input"}, {tok_eol, "newline"},
      {tok_error, "error"},      {tok_def, "'def'"},
      {tok_name, "name"}, {tok_number, "number"},
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

The named tokens (`tok_eof`, `tok_name`, and so on) go in by hand, since those need specific words, not a character. Everything from `0` to `255` gets a name generated from the loop: printable characters become `'x'`, common whitespace gets an escape sequence, and anything else falls back to a hex code like `0x07`. I need the loop to run once and the result to stay put, so I wrap it in a lambda and call it immediately — the `()` right after the closing `}`. `TokenNames` ends up holding the lambda's return value, not the lambda itself.

Two tokens need more than a name — I want the actual text the user typed, not just "name" or "number":

```cpp
static string FormatTokenForMessage(int Tok) {
  if (Tok == tok_name)
    return "name '" + Name + "'";
  if (Tok == tok_number)
    return "number '" + NumberLiteral + "'";

  auto It = TokenNames.find(Tok);
  if (It != TokenNames.end())
    return It->second;
  return "unknown token";
}
```

`Name` and `NumberLiteral` are the same globals the lexer already fills in when it reads an identifier or a number. Everything else just looks up its static name in `TokenNames`.

## Collapsing Punctuation Into Raw Characters

Chapters 1 through 3 gave every punctuation character its own named token: `tok_lparen`, `tok_rparen`, `tok_comma`, `tok_colon`, `tok_plus`, and so on, each requiring a `case` in `getToken()`'s switch to map the character to the name. Now that `TokenNames` can already produce a readable name for *any* character value, that indirection buys nothing. `tok_lparen` only ever meant `'('`; I can compare `CurrentToken == '('` directly and get the same clarity without a name that exists purely to stand in for a character.

So `getToken()` stops mapping punctuation to named tokens and just returns the character itself:

```cpp
int ThisChar = LastChar;
LastChar = advance();
return ThisChar;
```

No `switch`, no per-character token to declare when I add a new operator. Every place that used to compare against a named punctuation token now compares against the character directly — `CurrentToken != tok_rparen` becomes `CurrentToken != ')'`, `CurrentToken == tok_star || CurrentToken == tok_slash` becomes `CurrentToken == '*' || CurrentToken == '/'`, and so on throughout the parser.

This only works because the tokens that *do* need a name are negative, and a raw character byte never is:

```cpp
enum Token {
  tok_eof = -1,
  tok_eol = -2,
  tok_error = -3,

  // commands
  tok_def = -4,

  // primary
  tok_name = -5,
  tok_number = -6
};
```

`getToken()` returns an `int`, wide enough to hold either a byte value (`0`-`255`) or one of these negative sentinels, and the two ranges can never collide. `CurrentToken == tok_name` and `CurrentToken == '('` are both just integer comparisons against the same variable — the enum values only need to stay out of the byte range to make that safe.

## Tracking Where I Am

To report `(Line 3, Column 8)`, I need to know the line and column as I read characters. Two globals track position:

```cpp
struct SourceLocation {
  int Line;
  int Col;
};
static SourceLocation CurLoc;
static SourceLocation LexLoc = {1, 0};
```

`LexLoc` is where the lexer's read head currently is — it moves every time `advance()` reads a character. `CurLoc` is a snapshot taken once per token, at the start of `getToken()`, before any of the token's own characters are consumed. That's the position the parser and the diagnostics code actually see.

`advance()` already normalizes line endings; now it also updates `LexLoc`:

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

A newline bumps `Line` and resets `Col` to `0`; anything else just bumps `Col`. (`PyxcSourceMgr.onChar` is the source-buffering piece — next section.)

`getToken()` snapshots `LexLoc` into `CurLoc` right after the whitespace-skip loop, before looking at what kind of token follows:

```cpp
while (isspace(LastChar) && LastChar != '\n')
  LastChar = advance();

CurLoc = LexLoc;
```

Snapshotting here — after whitespace, before the token's own characters — means `CurLoc` lands on the first real character of whatever comes next.

There's one place this snapshot needs a second pass: a comment. `getToken()` snapshots `CurLoc` at the top of the function, pointed at `#`, then consumes the entire rest of the line before it can tell it's about to return `tok_eol`. Without a re-snapshot, an error on the *next* line would report a stale column from the comment line:

```cpp
if (LastChar == '#') {
  do
    LastChar = advance();
  while (LastChar != EOF && LastChar != '\n');

  if (LastChar != EOF) {
    CurLoc = LexLoc; // re-snapshot after consuming the whole comment + '\n'
    LastChar = ' ';
    return tok_eol;
  }
}
```

## Buffering Source Lines for the Caret

Knowing *where* the error is isn't enough to print the line it's on — I also need the line's actual text. `SourceManager` buffers it as the lexer reads:

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

`onChar` is called from `advance()` for every character consumed, so `SourceManager` sees the same stream the lexer does — no other part of the lexer has to know it exists. On `\n`, the finished line moves into `CompletedLines` and `CurrentLine` starts over. `getLine(N)` hands back a pointer to line `N` (1-based): a finished line from the vector, or `CurrentLine` itself if `N` is the line still being read.

## Printing the Caret

With a stored line and a column, the caret is just string formatting:

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

Print the line, then `Col - 1` spaces, then `^~~~`. The `-1` converts a 1-based column into a 0-based offset into the line.

## Pointing at the Right Place for a Newline

Most errors fire on a token that's sitting right where the problem is — a missing `)`, a stray `,`. A missing `:` is different: the parser doesn't discover it's missing until it reads the *next* token, which by then is `tok_eol`, on the line *after* the one that actually needs fixing.

By the time `getToken()` returns `tok_eol`, `advance()` has already consumed the `\n` and incremented `LexLoc.Line` — so `CurLoc.Line` for a `tok_eol` token is already the next line, one too many. `GetDiagnosticAnchorLoc` corrects for this specifically:

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

For any token other than `tok_eol`, `Loc` is correct as-is and comes back unchanged. For `tok_eol`, I step back one line and report a column one past that line's last character — just after wherever the missing `:` should have gone:

```
Error (Line 1, Column 12): Expected ':' in function definition
def bad(x) return 
           ^~~~
```

## Wiring It Into LogError

`LogErrorExpression` — the function every parse error already routes through — now builds on all of the above instead of printing a bare token description:

```cpp
unique_ptr<ExpressionNode> LogErrorExpression(const char *Str) {
  SourceLocation Anchor = GetDiagnosticAnchorLoc(CurLoc, CurrentToken);
  fprintf(stderr, "Error (Line %d, Column %d): %s\n", Anchor.Line, Anchor.Col,
          Str);
  PrintErrorSourceContext(Anchor);
  return nullptr;
}
```

`LogErrorSignature` and `LogErrorFunction` just call `LogErrorExpression`, so every parse error gets the location and the caret for free, no matter which of the three it goes through.

## Catching Malformed Numbers

`strtod` converts a string to a `double`, and it tells you where it stopped through an output parameter:

```cpp
char *End = nullptr;
NumberValue = strtod(NumStr.c_str(), &End);
```

If `End` points at the string's null terminator, every character was consumed — the whole thing was a valid number. If it points anywhere else, something was left over, and the input wasn't actually valid. Chapter 3's lexer never checked `End`; it just took whatever `strtod` managed to parse and moved on, which is how `1.2.3` quietly became `1.2`.

```cpp
NumberLiteral = NumStr;
char *End = nullptr;
NumberValue = strtod(NumStr.c_str(), &End);
if (!End || *End != '\0') {
  LogInvalidNumberLiteralAtLoc(NumStr, CurLoc);
  return tok_error;
}
return tok_number;
```

For `"1.2.3"`, `strtod` stops at the second `.`, so `End` points at `.3` — not the terminator. I report it and return `tok_error` instead of `tok_number`, using the same caret machinery from earlier:

```cpp
static void LogInvalidNumberLiteralAtLoc(const string &Literal, SourceLocation Loc) {
  fprintf(stderr, "Error (Line %d, Column %d): invalid number literal '%s'\n",
          Loc.Line, Loc.Col, Literal.c_str());
  PrintErrorSourceContext(Loc);
}
```

This lives in `getToken()`, which returns a plain `int`, so I can't return `LogErrorExpression(...)` here the way parser code does — there's no `nullptr` for an `int` to mean "error." Calling the helper directly and returning `tok_error` is the lexer's equivalent.

## Recovering From Errors

`tok_error` isn't a token the parser knows what to do with — it's not a number, not a name, not an operator. If `MainLoop` dispatched it to `ParsePrimary` like anything else, `ParsePrimary`'s `default` branch would hit and print a *second*, unrelated error on top of the one the lexer already printed for the bad number. So `MainLoop` intercepts it first:

```cpp
if (CurrentToken == tok_error) {
  SynchronizeToLineBoundary();
  continue;
}
```

```cpp
static void SynchronizeToLineBoundary() {
  while (CurrentToken != tok_eol && CurrentToken != tok_eof)
    getNextToken();
}
```

This is **panic-mode recovery**: once something's gone wrong badly enough that I can't reason about what state the parser is in, I stop trying to interpret the rest of the line and just advance past it, unconditionally, until I reach a token I know how to handle again. It throws away information — anything else on that line is gone — but it's simple and it always terminates in a known state: after `SynchronizeToLineBoundary()`, `CurrentToken` is always `tok_eol` or `tok_eof`.

The same recovery now also covers a case chapter 3 didn't catch at all: a line that parses *successfully* but has junk left over afterward. `HandleFunctionDefinition` and `HandleTopLevelExpression` both check for this once parsing succeeds:

```cpp
static void HandleTopLevelExpression() {
  if (ParseTopLevelExpression()) {
    if (CurrentToken != tok_eol && CurrentToken != tok_eof) {
      LogErrorExpression(("Unexpected " + FormatTokenForMessage(CurrentToken)).c_str());
      SynchronizeToLineBoundary();
      return;
    }
    fprintf(stderr, "Parsed a top-level expression.\n");
  } else {
    SynchronizeToLineBoundary();
  }
}
```

`3 = 10` is a good example: `3` parses as a complete, valid top-level expression, and the parser is done with it — `=` was never part of the grammar, so nothing asks for it. Chapter 3 would have just silently printed `Parsed a top-level expression.` and ignored `= 10` entirely. Now that leftover `=` fails the trailing-token check, and `FormatTokenForMessage` names exactly what was found where nothing more was expected.

`HandleFunctionDefinition` follows the identical shape for function definitions. Both call `SynchronizeToLineBoundary()` on any failure, successful-but-trailing-garbage included, so the REPL always lands back at a fresh `tok_eol`/`tok_eof` boundary before the next prompt.

## Build and Run

```bash
cd code/chapter-04
cmake -S . -B build && cmake --build build
./build/pyxc
```

```bash
llvm-lit test/
```

## Try It

```pyxc
ready> def add(x, y):
   x + y
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
ready>
```

## What's Next

The lexer and parser report real diagnostics now: a location, the source line, a caret. The grammar itself hasn't changed this chapter — every rule from Chapter 3 still applies exactly as written. What changed is how failure is reported and recovered from.

[Chapter 5](chapter-05.md) covers installing LLVM and setting up the build system — infrastructure I need before I can start turning this AST into real machine code.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version` and `ninja --version`

We'll figure it out.
