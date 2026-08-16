---
description: "Give every token a readable name, track source locations through the lexer, and print caret-style diagnostics instead of a bare token description."
---
# 5. pyxc: Better Errors

## What I Am Building

### The Missing Colon

Right now, a missing `:` looks like this:

<!-- code-merge:start -->
```pyxc
ready> def bad(x) return x
```
```text
Error: Expected ':' in function definition (token: name)
```
<!-- code-merge:end -->

`(token: name)` doesn't tell me which name, or where. I want this instead:

<!-- code-merge:start -->
```pyxc
ready> def bad(x) return x
```
```text
Error (Line 1, Column 12): Expected ':' in function definition
def bad(x) return 
           ^~~~
```
<!-- code-merge:end -->

Line. Column. The actual source line. A caret pointing at the exact spot.

### The Truncated Number

There's a second bug I've been ignoring: `1.2.3` currently parses without complaint.

<!-- code-merge:start -->
```pyxc
ready> 1.2.3
```
```text
Parsed a top-level expression.
```
<!-- code-merge:end -->

`strtod` reads as much of `"1.2.3"` as looks like a number (`1.2`) and silently ignores the rest.

### The Unknown Character

There's a third problem, for a character I never planned for at all. `@` isn't part of pyxc's grammar. In Chapter 3, I have no `case` for it, so I return `tok_error` from the generic `default` branch:

<!-- code-merge:start -->
```pyxc
ready> 1 @ 2
```
```text
Parsed a top-level expression.
Error: unknown token when expecting an expression (token: error)
Parsed a top-level expression.
```
<!-- code-merge:end -->

`(token: error)` doesn't say what the character even was, and one bad character turns into three confusing REPL lines. I want this instead:

<!-- code-merge:start -->
```pyxc
ready> 1 @ 2
```
```text
Error (Line 1, Column 3): Unexpected '@'
1 @ 
  ^~~~
```
<!-- code-merge:end -->

I fix all three problems in this chapter.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-05
```

## Naming Every Token

When I report an error, I want to show the token I saw. In my old `TokenNames` map, I added only the tokens I had declared by hand. If I encountered any other character, I had no readable name for it.

I fix this by giving every possible byte value a name. I build the map once, when the program starts:

```cpp
static map<int, string> TokenNames = [] {
  // I list tokens that are not single characters.
  static map<int, string> Names = {
      {tok_eof, "end of input"}, {tok_eol, "newline"},
      {tok_error, "error"},      {tok_def, "'def'"},
      {tok_name, "name"}, {tok_number, "number"},
  };

  // I add a readable name for every single-character value.
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

I format each byte in one of three ways:

- For a printable byte, I show the quoted character: `'('`, `'a'`, or `'!'`.
- For common whitespace, I show an escape sequence: `'\n'` or `'\t'`.
- For anything else, I show its hexadecimal value: `0x07`.

I put this code in a lambda and call it immediately with the final `()`. This lets me construct `TokenNames` once at startup.

I also assign every named punctuation token its actual character value. I keep the other tokens negative so they cannot collide with any byte value:

```cppdiff
*enum Token {
-  tok_eof = 1,
-  tok_eol,
-  tok_error,
-  tok_def,
-  tok_name,
-  tok_number,
-  tok_lparen,
-  tok_rparen,
-  tok_comma,
-  tok_colon,
-  tok_plus,
-  tok_minus,
-  tok_star,
-  tok_slash,
-  tok_percent,
-  tok_less,
+  tok_eof = -1,
+  tok_eol = -2,
+  tok_error = -3,
+  tok_def = -4,
+  tok_name = -5,
+  tok_number = -6,
+  tok_lparen = '(',
+  tok_rparen = ')',
+  tok_comma = ',',
+  tok_colon = ':',
+  tok_plus = '+',
+  tok_minus = '-',
+  tok_star = '*',
+  tok_slash = '/',
+  tok_percent = '%',
+  tok_less = '<',
*};
```

Because I define `tok_lparen = '('`, `tok_lparen` and `'('` are the same map key. In the loop, I add `Names['(']`. Later, `Names[tok_lparen]` and `Names['(']` are equivalent lookups and find the same entry. This also works for `tok_plus`, `tok_star`, and every other named character token, so I do not list them separately in the map. I also use the loop to name characters I did not declare as tokens.

## Buffering Source Lines

To print the line where the error occurred, I need the text of that line. I use `SourceManager` to store each line as I read it:

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
    // I may need the current line before I have consumed its newline.
    if (Index == CompletedLines.size())
      return &CurrentLine;
    return nullptr;
  }
};

static SourceManager PyxcSourceManager;
```

I call `onChar()` from `advance()` for every character I read. I add ordinary characters to `CurrentLine`. When I reach `\n`, I move the completed line into `CompletedLines` and clear `CurrentLine` for the next one.

In `getLine()`, I convert the requested line number from 1-based to 0-based. This lets me retrieve a line both while I am reading it and after I have completed it.

## Tracking Where I Am

To report `(Line 3, Column 8)`, I need to record the line and column as I read each character. I use two globals:

```cpp
struct SourceLocation {
  int Line;
  int Column;
};
static SourceLocation CurrentTokenLocation;
static SourceLocation LexerLocation = {1, 0};
```

I use `LexerLocation` to record how far I have read. I update it every time I read a character in `advance()`. I use `CurrentTokenLocation` to record where the current token starts. I read `CurrentTokenLocation` in the parser and in my diagnostics.

I already use `advance()` to normalize line endings. I now update `LexerLocation` there too, and feed every character to `PyxcSourceManager.onChar()` from the previous section, so it can buffer the line I'm currently on:

```cppdiff
*static int advance() {
*  int LastChar = getchar();
*
*  // case: '\r' or '\r\n'
*  if (LastChar == '\r') {
*    int NextChar = getchar();
*
*    // A following '\n' is part of the same line ending; eat it.
*    // Anything else belongs to the next token; put it back.
*    // (EOF can't be put back at all, so it's excluded from that check.
*    // The next getchar() will still return EOF, so we don't lose it.)
*    if (NextChar != '\n' && NextChar != EOF) {
*      ungetc(NextChar, stdin);
*    }
+    PyxcSourceManager.onChar('\n');
+    LexerLocation.Line++;
+    LexerLocation.Column = 0;
*    return '\n';
*  }
*
+  // '\n' resets Column and starts a new buffered line; anything else
+  // just advances Column within the current line.
+  if (LastChar == '\n') {
+    PyxcSourceManager.onChar('\n');
+    LexerLocation.Line++;
+    LexerLocation.Column = 0;
+  } else {
+    PyxcSourceManager.onChar(LastChar);
+    LexerLocation.Column++;
+  }
+
*  // case '\n' or any other non-newline character
*  return LastChar;
*}
```

When I read a newline, I increment `Line` and reset `Column` to `0`. For any other character, I increment only `Column`.

In `getToken()`, I copy `LexerLocation` into `CurrentTokenLocation` after I skip whitespace but before I read the token itself:

```cppdiff
*static int getToken() {
*  static int LastChar = ' ';
*
*  while (isspace(LastChar) && LastChar != '\n')
*    LastChar = advance();
*
+  CurrentTokenLocation = LexerLocation;
+
*  if (LastChar == '\n') {
*    LastChar = ' ';
*    return tok_eol;
*  }
*  ...
*}
```

By copying the location here, I make `CurrentTokenLocation` point at the token's first character rather than any whitespace before it.

I need to copy the location again after a comment. At the start of `getToken()`, I set `CurrentTokenLocation` to the position of `#`. I then consume the rest of the line and return `tok_eol`. If I leave `CurrentTokenLocation` at `#`, an error on the next line can report a column from the comment line. I avoid that by copying `LexerLocation` again after I consume the newline:

```cppdiff
*static int getToken() {
*  ...
*  // I discard a comment.
*  if (LastChar == '#') {
*    // I consume characters through the end of the line.
*    do {
*      LastChar = advance();
*    } while (LastChar != '\n' && LastChar != EOF);
*
*    if (LastChar == '\n') {
+      CurrentTokenLocation = LexerLocation;
*      LastChar = ' ';
*      return tok_eol;
*    }
*  }
*  ...
*}
```

## Printing the Caret

Once I have the line text and column, I can print the caret:

```cpp
static void PrintErrorSourceContext(SourceLocation Location) {
  const string *LineText = PyxcSourceManager.getLine(Location.Line);
  // LineText is null only if Location points past everything buffered so
  // far (e.g. an uninitialized Location.Line == 0). Skip printing rather
  // than dereference it below.
  if (!LineText)
    return;

  fprintf(stderr, "%s\n", LineText->c_str());
  int spaces = Location.Column - 1;
  // I guard against an invalid column before printing the spaces.
  if (spaces < 0)
    spaces = 0;
  for (int i = 0; i < spaces; ++i)
    fputc(' ', stderr);
  fprintf(stderr, "^~~~\n");
}
```

I print the line, then `Column - 1` spaces, then `^~~~`. I subtract one because the column is 1-based but the offset into the line is 0-based.

## Pointing at the Right Place for a Newline

For most errors, `CurrentTokenLocation` already points where I need it. A missing `:` is different. I do not know it is missing until I ask for the next token and receive `tok_eol`.

Before I return `tok_eol`, I have already consumed the `\n` and incremented `LexerLocation.Line`. That leaves `CurrentTokenLocation.Line` on the next line. I correct this in `GetCaretAnchorLocation()`:

```cpp
static SourceLocation GetCaretAnchorLocation(SourceLocation Location, int Token) {
  if (Token != tok_eol || Location.Line <= 1)
    return Location;

  // Token == tok_eol && Location.Line > 1. I need to return a location just
  // past the end of the previous line.
  int PrevLine = Location.Line - 1;
  const string *PrevLineText = PyxcSourceManager.getLine(PrevLine);
  // PrevLineText is null only if PrevLine hasn't been buffered yet —
  // it shouldn't happen, since I only get here after consuming that
  // line's trailing newline, but I fall back to the original Location
  // rather than trust an out-of-range read.
  if (!PrevLineText)
    return Location;

  return {PrevLine, static_cast<int>(PrevLineText->size()) + 1};
}
```

For any token other than `tok_eol`, or if there is no previous line, I return
`Location` unchanged. Otherwise, I step back one line and report the column
just after that line's last character.

<!-- code-merge:start -->
```pyxc
ready> def missing_colon(x)
```
```text
Error (Line 5, Column 21): Expected ':' in function definition
def missing_colon(x)
                    ^~~~
```
<!-- code-merge:end -->

## Recovering from Errors

After I report a lexer error, I return `tok_error`. I do not want to parse it as a number, name, or operator because that would print a second, unrelated error. I call this **panic-mode recovery**: once I can no longer trust the current parse, I stop interpreting the line. I skip tokens until I reach `tok_eol` or `tok_eof`. I discard the rest of the line, but I return to a state where I know how to continue.

```cpp
static void DiscardRestOfLine() {
  // I stop at tok_eol or tok_eof without consuming it, so MainLoop()
  // can handle it.
  while (CurrentToken != tok_eol && CurrentToken != tok_eof)
    getNextToken();
}
```

## Wiring Diagnostics into Error Reporting

I already report every parse error through `LogErrorExpression()`. I now use the location and source line there instead of printing only a token description:

```cppdiff
*unique_ptr<ExpressionNode> LogErrorExpression(const char *ErrorMessage) {
+  SourceLocation Anchor = GetCaretAnchorLocation(CurrentTokenLocation, CurrentToken);
-  fprintf(stderr, "Error: %s (token: %s)\nready> ", ErrorMessage,
-          TokenNames.at(CurrentToken).c_str());
+  fprintf(stderr, "Error (Line %d, Column %d): %s\n", Anchor.Line, Anchor.Column,
+          ErrorMessage);
+  PrintErrorSourceContext(Anchor);
*  return nullptr;
*}
```

I also drop the `\nready> ` this function used to print right after the message. In Chapters 2 and 4, `LogErrorExpression()` printed the next prompt itself, and if the error happened to land just before a bare newline, `MainLoop()`'s own newline handling printed a second one, so I'd see `ready> ready> `. Now every error path calls `DiscardRestOfLine()` before returning, so `MainLoop()` sees `tok_eol` or `tok_eof` itself and prints the prompt exactly once.

I keep `LogErrorSignature()` and `LogErrorFunction()` as small wrappers around `LogErrorExpression()`. By doing this, I give every parse error the same location and caret output.

## Catching Malformed Numbers

I use `strtod` to convert a string to a `double`. I pass it an output parameter named `End` so I can see where the conversion stopped. In Chapter 3, I ignored `End` and accepted whatever prefix `strtod` could convert. That is how `1.2.3` quietly became `1.2`.

When part of the input is invalid, I need to report it. I add a small helper for that:

```cpp
static void LogInvalidNumberLiteralAtLocation(const string &Literal, SourceLocation Loc) {
  fprintf(stderr, "Error (Line %d, Column %d): invalid number literal '%s'\n",
          Loc.Line, Loc.Column, Literal.c_str());
  PrintErrorSourceContext(Loc);
}
```

I call it from `getToken()`'s number-reading branch. While I'm there, I also promote `NumberLiteral` from a local variable to a file-scope global (declared alongside `NumberValue`), so I can still read the exact source text after `getToken()` returns, for diagnostics elsewhere in this chapter:

```cppdiff
*  if (isdigit(LastChar) || LastChar == '.') {
-    string NumberLiteral;
+    NumberLiteral.clear();
*    do {
*      NumberLiteral += LastChar;
*      LastChar = advance();
*    } while (isdigit(LastChar) || LastChar == '.');
*
-    // TODO: I consume all of 1.23.45.67 but parse it as 1.23.
-    NumberValue = strtod(NumberLiteral.c_str(), 0);
+    char *End = nullptr;
+    NumberValue = strtod(NumberLiteral.c_str(), &End);
+    if (!End || *End != '\0') {
+      LogInvalidNumberLiteralAtLocation(NumberLiteral, CurrentTokenLocation);
+      return tok_error;
+    }
*    return tok_number;
*  }
```

If `End` points at the string's null terminator, I know `strtod` consumed every character. If it points anywhere else, part of the input was invalid: for `"1.2.3"`, `strtod` stops at the second `.`, so `End` points at `.3` rather than the terminator. I report the invalid number and return `tok_error` instead of `tok_number`. I do this in `getToken()`, which returns an `int`. I cannot return `nullptr` as I do from a parsing function. Instead, I call the error helper and return `tok_error`.

## Naming Unknown Characters Too

`@` isn't punctuation I recognize. Previously, the lexer's `default` case discarded the character and returned the generic `tok_error`:

```cppdiff
*  switch (ThisChar) {
*  ...
-  default:
-    return tok_error;
+  default:
+    return ThisChar;
*  }
```

I return `ThisChar` instead so I can name the character that caused the error.

For names and numbers, I want to include the actual text from the source rather than report only `name` or `number`. I already have `Name` from Chapter 1 for the name case, and I just promoted `NumberLiteral` to a file-scope global above, in [Catching Malformed Numbers](#catching-malformed-numbers), for exactly this reason.

With those source spellings available, I define one helper for every diagnostic that needs to name a token:

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

Names and numbers use their lexer globals so the message includes the original text. Every other token uses its `TokenNames` entry, including an unknown character such as `@` that the lexer now returns directly.

Chapter 4 already made `ParsePrimary()` name an unexpected token through `TokenNames`. I now route that same `default` case through `FormatTokenForMessage()` so it uses the richer formatting for every token kind:

```cppdiff
*  default:
*    return LogErrorExpression(
-        ("Unexpected " + TokenNames.at(CurrentToken)).c_str());
+        ("Unexpected " + FormatTokenForMessage(CurrentToken)).c_str());
```

A bare `@` at the start of a line now reports:

<!-- code-merge:start -->
```pyxc
ready> @
```
```text
Error (Line 1, Column 1): Unexpected '@'
@
^~~~
```
<!-- code-merge:end -->

I use that formatter again when a character appears as an unexpected *trailing* token after an otherwise complete expression, a case `ParsePrimary()` never runs for. I cover that check next, in [Printing the Prompt Exactly Once](#printing-the-prompt-exactly-once); it turns this chapter's opening `1 @ 2` example into the same kind of message:

<!-- code-merge:start -->
```pyxc
ready> 1 @ 2
```
```text
Error (Line 1, Column 3): Unexpected '@'
1 @ 
  ^~~~
```
<!-- code-merge:end -->

## Printing the Prompt Exactly Once

I handle `tok_error` directly in `MainLoop()`'s switch before it can fall through to a parsing function, and call `DiscardRestOfLine()`:

```cppdiff
*static void MainLoop() {
*  while (CurrentToken != tok_eof) {
*    switch (CurrentToken) {
*    case tok_eol:
*      // For a bare newline, I print a fresh prompt and read the next token.
*      fprintf(stderr, "ready> ");
*      getNextToken();
*      break;
*    case tok_error:
-      LogErrorExpression("invalid character");
-      getNextToken();
+      DiscardRestOfLine();
*      break;
*    case tok_def:
*      HandleFunctionDefinition();
*      break;
*    default:
*      HandleTopLevelExpression();
*      break;
*    }
*  }
*}
```

I use the same recovery when I parse a valid construct but find extra tokens after it. In both `HandleFunctionDefinition()` and `HandleTopLevelExpression()`, I check that parsing stopped at `tok_eol` or `tok_eof`:

```cpp
static void HandleTopLevelExpression() {
  if (ParseTopLevelExpression()) {
    if (CurrentToken != tok_eol && CurrentToken != tok_eof) {
      LogErrorExpression(("Unexpected " + FormatTokenForMessage(CurrentToken)).c_str());
      DiscardRestOfLine();
      return;
    }
    fprintf(stderr, "Parsed a top-level expression.\n");
  } else {
    DiscardRestOfLine();
  }
}
```

For example, when I parse `3 = 10`, I can accept `3` as a complete top-level expression and leave `= 10` unread. In Chapter 3, I printed `Parsed a top-level expression.` and ignored the rest. Now I check for unread tokens, report the unexpected `=`, and discard the rest of the line.

I make `HandleFunctionDefinition()` perform the same check for function definitions. After any failure, including extra trailing tokens, I call `DiscardRestOfLine()` before I print the next prompt.

## Build and Run

```bash
cd code/chapter-05
cmake -S . -B build && cmake --build build
./build/pyxc
```

```bash
llvm-lit -v test/
```

## Try It

<!-- code-merge:start -->
```pyxc
ready> def add(x, y):
   x + y
```
```text
Parsed a function definition.
```
<!-- code-merge:end -->
<!-- code-merge:start -->
```pyxc
ready> 1.2.3
```
```text
Error (Line 3, Column 1): invalid number literal '1.2.3'
1.2.3
^~~~
```
<!-- code-merge:end -->
<!-- code-merge:start -->
```pyxc
ready> def bad(x) return x
```
```text
Error (Line 4, Column 12): Expected ':' in function definition
def bad(x) return 
           ^~~~
```
<!-- code-merge:end -->
<!-- code-merge:start -->
```pyxc
ready> def missing_colon(x)
```
```text
Error (Line 5, Column 21): Expected ':' in function definition
def missing_colon(x)
                    ^~~~
```
<!-- code-merge:end -->
<!-- code-merge:start -->
```pyxc
ready> @
```
```text
Error (Line 6, Column 1): Unexpected '@'
@
^~~~
```
<!-- code-merge:end -->
<!-- code-merge:start -->
```pyxc
ready> 1 @ 2
```
```text
Error (Line 7, Column 3): Unexpected '@'
1 @ 
  ^~~~
```
<!-- code-merge:end -->

## What's Next

[Chapter 6](chapter-06.md) gets LLVM installed and ready.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:

- Your OS and version
- Full error message
- Output of `cmake --version`

I'll help you figure it out.
