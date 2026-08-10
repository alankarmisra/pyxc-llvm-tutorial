---
description: "Give every token a readable name, track source locations through the lexer, and print caret-style diagnostics instead of a bare token description."
---
# 4. pyxc: Better Errors

## Where I Am

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

```pyxc
ready> 1.2.3
Parsed a top-level expression.
```

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
cd pyxc-llvm-tutorial/code/chapter-04
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
 enum Token {
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
+  tok_less = '<',
 };
```

Because I define `tok_lparen = '('`, `tok_lparen` and `'('` are the same map key. In the loop, I add `Names['(']`. Later, `Names[tok_lparen]` and `Names['(']` are equivalent lookups and find the same entry. This also works for `tok_plus`, `tok_star`, and every other named character token, so I do not list them separately in the map. I also use the loop to name characters I did not declare as tokens.

For names and numbers, I want to include the actual text from the source rather than report only `name` or `number`:

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

I read that text from the lexer's `Name` and `NumberLiteral` globals. For every other token, I use the name stored in `TokenNames`.

## Tracking Where I Am

To report `(Line 3, Column 8)`, I need to record the line and column as I read each character. I use two globals:

```cpp
struct SourceLocation {
  int Line;
  int Col;
};
static SourceLocation CurLoc;
static SourceLocation LexLoc = {1, 0};
```

I use `LexLoc` to record how far I have read. I update it every time I read a character in `advance()`. I use `CurLoc` to record where the current token starts. I read `CurLoc` in the parser and in my diagnostics.

I already use `advance()` to normalize line endings. I now update `LexLoc` there too:

```cpp
static int advance() {
  int LastChar = getchar();
  if (LastChar == '\r') {
    int NextChar = getchar();
    if (NextChar != '\n' && NextChar != EOF) {
      // I read one character too far while checking for '\r\n', so I put it back.
      ungetc(NextChar, stdin);
    }
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

When I read a newline, I increment `Line` and reset `Col` to `0`. For any other character, I increment only `Col`. I explain `PyxcSourceMgr.onChar()` in the next section.

In `getToken()`, I copy `LexLoc` into `CurLoc` after I skip whitespace but before I read the token itself:

```cpp
while (isspace(LastChar) && LastChar != '\n')
  LastChar = advance();

CurLoc = LexLoc;
```

By copying the location here, I make `CurLoc` point at the token's first character rather than any whitespace before it.

I need to copy the location again after a comment. At the start of `getToken()`, I set `CurLoc` to the position of `#`. I then consume the rest of the line and return `tok_eol`. If I leave `CurLoc` at `#`, an error on the next line can report a column from the comment line. I avoid that by copying `LexLoc` again after I consume the newline:

```cpp
if (LastChar == '#') {
  do
    LastChar = advance();
  while (LastChar != EOF && LastChar != '\n');

  if (LastChar != EOF) {
    CurLoc = LexLoc;
    LastChar = ' ';
    return tok_eol;
  }
}
```

## Buffering Source Lines for the Caret

Knowing *where* the error is isn't enough. I also need the text of that line. I use `SourceManager` to store each line as I read it:

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

static SourceManager PyxcSourceMgr;
```

I call `onChar()` from `advance()` for every character I read. I add ordinary characters to `CurrentLine`. When I reach `\n`, I move the completed line into `CompletedLines` and clear `CurrentLine` for the next one.

In `getLine()`, I convert the requested line number from 1-based to 0-based. This lets me retrieve a line both while I am reading it and after I have completed it.

## Printing the Caret

Once I have the line text and column, I can print the caret:

```cpp
static void PrintErrorSourceContext(SourceLocation Loc) {
  const string *LineText = PyxcSourceMgr.getLine(Loc.Line);
  if (!LineText)
    return;

  fprintf(stderr, "%s\n", LineText->c_str());
  int spaces = Loc.Col - 1;
  // I guard against an invalid column before printing the spaces.
  if (spaces < 0)
    spaces = 0;
  for (int i = 0; i < spaces; ++i)
    fputc(' ', stderr);
  fprintf(stderr, "^~~~\n");
}
```

I print the line, then `Col - 1` spaces, then `^~~~`. I subtract one because the column is 1-based but the offset into the line is 0-based.

## Pointing at the Right Place for a Newline

For most errors, `CurLoc` already points where I need it. A missing `:` is different. I do not know it is missing until I ask for the next token and receive `tok_eol`.

Before I return `tok_eol`, I have already consumed the `\n` and incremented `LexLoc.Line`. That leaves `CurLoc.Line` on the next line. I correct this in `GetDiagnosticAnchorLoc()`:

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

For any token other than `tok_eol`, I return `Loc` unchanged. For `tok_eol`, I step back one line and report the column just after that line's last character. That is where the missing `:` should have gone:

```
Error (Line 1, Column 12): Expected ':' in function definition
def bad(x) return 
           ^~~~
```

## Wiring It Into LogError

I already report every parse error through `LogErrorExpression()`. I now use the location and source line there instead of printing only a token description:

```cpp
unique_ptr<ExpressionNode> LogErrorExpression(const char *Str) {
  SourceLocation Anchor = GetDiagnosticAnchorLoc(CurLoc, CurrentToken);
  fprintf(stderr, "Error (Line %d, Column %d): %s\n", Anchor.Line, Anchor.Col,
          Str);
  PrintErrorSourceContext(Anchor);
  return nullptr;
}
```

I keep `LogErrorSignature()` and `LogErrorFunction()` as small wrappers around `LogErrorExpression()`. By doing this, I give every parse error the same location and caret output.

## Catching Malformed Numbers

I use `strtod` to convert a string to a `double`. I pass it an output parameter named `End` so I can see where the conversion stopped:

```cpp
char *End = nullptr;
NumberValue = strtod(NumStr.c_str(), &End);
```

If `End` points at the string's null terminator, I know `strtod` consumed every character. If it points anywhere else, I know part of the input was invalid. In Chapter 3, I ignored `End` and accepted whatever prefix `strtod` could convert. That is how `1.2.3` quietly became `1.2`.

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

For `"1.2.3"`, `strtod` stops at the second `.`, so `End` points at `.3` rather than the terminator. I report the invalid number and return `tok_error` instead of `tok_number`:

```cpp
static void LogInvalidNumberLiteralAtLoc(const string &Literal, SourceLocation Loc) {
  fprintf(stderr, "Error (Line %d, Column %d): invalid number literal '%s'\n",
          Loc.Line, Loc.Col, Literal.c_str());
  PrintErrorSourceContext(Loc);
}
```

I do this in `getToken()`, which returns an `int`. I cannot return `nullptr` as I do from a parsing function. Instead, I call the error helper and return `tok_error`.

## Recovering From Errors

After I report a lexer error, I return `tok_error`. I do not want to parse it as a number, name, or operator because that would print a second, unrelated error. I check for it in `MainLoop()` before I call any parsing function:

```cpp
if (CurrentToken == tok_error) {
  SynchronizeToLineBoundary();
  continue;
}
```

```cpp
static void SynchronizeToLineBoundary() {
  // I leave the boundary token for MainLoop() to handle.
  while (CurrentToken != tok_eol && CurrentToken != tok_eof)
    getNextToken();
}
```

I call this **panic-mode recovery**. Once I can no longer trust the current parse, I stop interpreting the line. I skip tokens until I reach `tok_eol` or `tok_eof`. I discard the rest of the line, but I return to a state where I know how to continue.

I use the same recovery when I parse a valid construct but find extra tokens after it. In both `HandleFunctionDefinition()` and `HandleTopLevelExpression()`, I check that parsing stopped at `tok_eol` or `tok_eof`:

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

For example, when I parse `3 = 10`, I can accept `3` as a complete top-level expression and leave `= 10` unread. In Chapter 3, I printed `Parsed a top-level expression.` and ignored the rest. Now I check for unread tokens, report the unexpected `=`, and discard the rest of the line.

I make `HandleFunctionDefinition()` perform the same check for function definitions. After any failure, including extra trailing tokens, I call `SynchronizeToLineBoundary()` before I print the next prompt.

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

I now report a location, the source line, and a caret for lexer and parser errors. I did not change the grammar in this chapter; every rule from Chapter 3 still applies. I changed only how I report errors and recover from them.

[Chapter 5](chapter-05.md) covers installing LLVM and setting up the build system — infrastructure I need before I can start turning this AST into real machine code.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:

- Your OS and version
- Full error message
- Output of `cmake --version`

I'll help you figure it out.
