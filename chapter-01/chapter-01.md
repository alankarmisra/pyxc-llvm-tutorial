---
description: "Build your first lexer: break source code into tokens and see them print in real-time."
---
# 1. Pyxc: Lexer Basics

## What We're Building

Welcome to Pyxc! By the end of this tutorial, you'll have built a compiler for a statically-typed, Python-like language.

**This chapter** focuses on the lexer—breaking source code into tokens. We'll keep the language simple for now (just functions, numbers, and basic math), but here's a glimpse of where we're going:

```python
def fibonacci(n: int) -> int:
    if n <= 1:
        return n
    return fibonacci(n - 1) + fibonacci(n - 2)
```

Notice the type annotations (`: int`, `-> int`), comparison operator (`<=`), and `if` statement? We'll add those in later chapters. For now, let's start with the foundation: teaching the compiler to recognize words, numbers, and symbols.

Here's what Pyxc code looks like in **this chapter**:

```python
def fib(n):
    return fib(n-1) + fib(n-2)
```

We start simple: only one type (`double`), no type declarations, no control flow yet. Just functions, math, and recursion.

You can even call C library functions:

```python
extern def sin(x)
extern def cos(x)

sin(1.0) + cos(2.0)
```

That `extern` tells Pyxc "this function exists somewhere else, just trust me." LLVM handles the rest.

## Source Code

Grab the code: [chapter-01/code](https://github.com/alankarmisra/pyxc-llvm-tutorial/tree/main/chapter-01/code)

Or clone the whole repo:
```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/chapter-01/code
```

## What's a Lexer?

Before we can understand code, we need to break it into pieces. That's what a lexer does.

**Input:** Raw text
```python
def add(x, y):
    return x + y
```

**Output:** Tokens
```
'def' identifier '(' identifier ',' identifier ')' ':' newline
'return' identifier '+' identifier newline
```

Each "word" or symbol becomes a token. The lexer doesn't understand what `def` *means*—it just recognizes "this is a keyword called `def`." Understanding comes later (in the parser).

## Token Types

First, we define what kinds of tokens exist:

```cpp
enum Token {
  tok_eof = -1,        // End of file
  tok_eol = -2,        // End of line (newline)
  tok_error = -3,      // Lexing error (gettok() can return this)

  tok_def = -4,        // 'def' keyword
  tok_extern = -5,     // 'extern' keyword

  tok_identifier = -6, // Variable/function names
  tok_number = -7,     // Numbers like 3.14

  tok_return = -8      // 'return' keyword
};
```

Why negative numbers? Because we also return single-character tokens (like `+`, `(`, `)`) as their ASCII values (which are positive). This way, we don't collide.

Example:
- `tok_def` → -4
- `+` → 43 (its ASCII code)
- `(` → 40

We also have two globals to hold extra info:

```cpp
static string IdentifierStr;  // Holds the name (if it's an identifier)
static double NumVal;         // Holds the value (if it's a number)
```

When the lexer sees `foo`, it returns `tok_identifier` and sets `IdentifierStr = "foo"`.
When it sees `3.14`, it returns `tok_number` and sets `NumVal = 3.14`.

## Making Tokens Readable

For debugging, we want nice names instead of numbers:

```cpp
static map<int, string> TokenNames = [] {
  map<int, string> Names = {
      {tok_eof, "end of input"},
      {tok_eol, "newline"},
      {tok_error, "error"},
      {tok_def, "'def'"},
      {tok_extern, "'extern'"},
      {tok_identifier, "identifier"},
      {tok_number, "number"},
      {tok_return, "'return'"},
  };

  // Map printable characters to themselves
  for (int C = 0; C <= 255; ++C) {
    if (isprint(C))
      Names[C] = "'" + string(1, (char)C) + "'";
    else if (C == '\n')
      Names[C] = "'\\n'";
    // ... handle other special chars
  }
  return Names;
}();
```

Now we can print `'def'` instead of `-4`. Much easier to read.

## Tracking Source Locations

We track where we are in the file (line and column):

```cpp
struct SourceLocation {
  int Line;
  int Col;
};

static SourceLocation CurLoc; // Start of current token
static SourceLocation LexLoc 
    = {/* line */ 1, /* column */ 0};  // Current read position
```

Why track this? So error messages can say "Error on line 5, column 12" instead of just "Error."

We also save the source text as we read it:

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
      CurrentLine.push_back((char)C);
  }

  const string *getLine(int LineNumber) const {
    if (LineNumber <= 0)
      return nullptr;
    size_t Index = static_cast<size_t>(LineNumber - 1);
    if (Index < CompletedLines.size())
      return &CompletedLines[Index];
    if (Index == CompletedLines.size())
      return &CurrentLine; // Last line even without trailing newline.
    return nullptr;
  }
};

static SourceManager DiagSourceMgr;
```

Every character we read gets logged here. Later, we can show the exact line where an error occurred.

At EOF, `onChar()` does not push a final line into `CompletedLines` unless a newline was seen. That's okay: `getLine()` returns `CurrentLine` for the last line index, so diagnostics still see the final unterminated line. 

## Reading Characters

Instead of calling `getchar()` directly, we wrap it:

```cpp
static int advance() {
  int LastChar = getchar();

  // // Normalize `\r\n` (Windows) and bare `\r` (Old Macs) line endings to '\n'.
  if (LastChar == '\r') {
    int NextChar = getchar();
    if (NextChar != '\n' && NextChar != EOF)
      ungetc(NextChar, stdin);
    DiagSourceMgr.onChar('\n');
    LexLoc.Line++;
    LexLoc.Col = 0;
    return '\n';
  }

  if (LastChar == '\n') {
    DiagSourceMgr.onChar('\n');
    LexLoc.Line++;
    LexLoc.Col = 0;
  } else {
    DiagSourceMgr.onChar(LastChar);
    LexLoc.Col++;
  }

  return LastChar;
}
```

This does three things:
1. Reads a character
2. Updates line/column tracking
3. Normalizes line endings (so `\r\n` and `\r` (no `\n` as is the case on some old Macs) becomes `\n`)

Now `gettok()` can just call `advance()` and everything is tracked automatically.

## The Main Lexer: `gettok()`

This function reads characters and returns tokens:

```cpp
static int gettok() {
  // Note on initialization: ' ' is whitespace, so the skip loop below
  // immediately calls advance() to read the first real character.
  static int LastChar = ' '; 

  // Skip whitespace (but NOT newlines)
  while (isspace(LastChar) && LastChar != '\n')
    LastChar = advance();
```

We skip spaces and tabs, but we keep newlines because they matter in Python-like syntax.

### Recognizing Newlines

```cpp
  if (LastChar == '\n') {
    LastChar = ' ';
    return tok_eol;
  }

  CurLoc = LexLoc;  // Mark where this token starts
```

Newlines are significant — they end statements.

Notice we **don't** call `advance()` here. Every other token handler (identifiers, numbers, operators) ends by consuming one character past the token, leaving `LastChar` pointing at whatever comes next, ready for the next call to `gettok()`. The newline handler deliberately breaks that pattern.

Why? Because in the REPL, `advance()` calls `getchar()`, which blocks waiting for the user to type. If we called `advance()` after seeing `\n`, the REPL would freeze — waiting for the *next* line of input before it could return `tok_eol`, print the result, and show `ready>` again. The user would have to type a blank line just to see their output.

Instead we set `LastChar = ' '` (a safe whitespace initial value) and return immediately. The next call to `gettok()` will hit the whitespace-skip loop, which calls `advance()` only when it's actually time to read the next token.

### Recognizing Identifiers and Keywords

```cpp
  if (isalpha(LastChar) || LastChar == '_') {
    IdentifierStr = LastChar;
    while (isalnum((LastChar = advance())) || LastChar == '_')
      IdentifierStr += LastChar;

    // This is defined towards the top of the line in the code file.
    static map<string, Token> Keywords = {
        {"def", tok_def},
        {"extern", tok_extern},
        {"return", tok_return}
    };

    auto It = Keywords.find(IdentifierStr);
    return (It != Keywords.end()) ? It->second : tok_identifier;
  }
```

We read all letters/digits/underscores, then check if it matches a keyword. If not, it's a regular identifier.

Examples:
- `def` → `tok_def`
- `foo` → `tok_identifier` (and `IdentifierStr = "foo"`)
- `my_var` → `tok_identifier` (and `IdentifierStr = "my_var"`)

### Recognizing Numbers

```cpp
  if (isdigit(LastChar) || LastChar == '.') {
    string NumStr;
    do {
      NumStr += LastChar;
      LastChar = advance();
    } while (isdigit(LastChar) || LastChar == '.');

    char *End;
    NumVal = strtod(NumStr.c_str(), &End);
    if (*End != '\0') {
      fprintf(stderr, "Error (Line %d, Col %d): invalid number literal '%s'\n",
              LexLoc.Line, LexLoc.Col, NumStr.c_str());
      return tok_error;
    }
    return tok_number;
  }
```

We grab all digits and decimal points into `NumStr`, then convert with `strtod`.

`strtod` takes a second argument — a pointer it sets to the first character it *couldn't* parse. If `*End != '\0'`, there's leftover junk in the string, meaning the number was malformed. For example, `1.2.3` puts `"1.2.3"` into `NumStr`; `strtod` parses `1.2` and sets `End` pointing at `.3`. We catch that and emit an error rather than silently returning the wrong value.

When this happens, `gettok()` returns `tok_error` so the caller can recover and keep lexing the next token.

Examples:
- `42` → `tok_number` (and `NumVal = 42.0`)
- `3.14` → `tok_number` (and `NumVal = 3.14`)
- `.5` → `tok_number` (and `NumVal = 0.5`)
- `1.2.3` → `tok_error` with message `invalid number literal '1.2.3'`

### Recognizing Comments

```cpp
  if (LastChar == '#') {
    // Discard until end of line.
    do
      LastChar = advance();
    while (LastChar != EOF && LastChar != '\n');

    if (LastChar != EOF) {
      LastChar = ' ';  // Don't wait for a new character, otherwise REPL() will freeze.
      return tok_eol;
    }
  }
```

Comments run from `#` to the end of the line. We skip them and return a single `tok_eol`.

If a comment reaches EOF without a trailing newline, `gettok()` returns `tok_eof` (not `tok_eol`) because `LastChar` is `EOF` and falls through to the EOF case.

### Everything Else

```cpp
  if (LastChar == EOF)
    return tok_eof;

  // Return single-character tokens as their ASCII value
  int ThisChar = LastChar;
  LastChar = advance();
  return ThisChar;
}
```

If it's not a keyword, identifier, number, or comment, it's a single character like `+`, `(`, or `:`. We return its ASCII value directly.

## Testing the Lexer

We build a simple REPL that prints tokens as they appear:

```cpp
void MainLoop() {
  fprintf(stderr, "ready> ");
  while (true) {
    int Tok = gettok();
    if (Tok == tok_eof)
      break;

    if (Tok == tok_error)
      continue;

    fprintf(stderr, "%s", GetTokenName(Tok).c_str());
    if (Tok == tok_eol)
      fprintf(stderr, "\nready> ");
    else
      fprintf(stderr, " ");
  }
}

int main() {
  DiagSourceMgr.reset();
  MainLoop();
  return 0;
}
```

`MainLoop()` intentionally skips `tok_error` tokens via `continue`, so one malformed token doesn't kill the REPL session.

## Compile and Run

```bash
cd chapter-01/code
./build.sh  # macOS/Linux
```

Or manually:
```bash
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
./build/pyxc
```

## Sample Output

```
ready> def add(a, b):
'def' identifier '(' identifier ',' identifier ')' ':' newline
ready> return a + b
'return' identifier '+' identifier newline
ready> extern def sin(x)
'extern' 'def' identifier '(' identifier ')' newline
ready> 1 + 3.1.4
number '+' Error (Line 4, Col 5): invalid number literal '3.1.4'
newline
ready> ^D
```

Notice:
- Keywords like `def` show up as `'def'`
- Names like `add` show up as `identifier`
- Operators like `+` show up as `'+'`
- Newlines show up as `newline`

The lexer is working!

## What We Built

- **Token types** - Enum for keywords, identifiers, numbers, etc.
- **`gettok()`** - Main lexer function
- **Source tracking** - Line/column info for error messages
- **Simple REPL** - See tokens in real-time

## What's Next

The lexer doesn't *understand* code—it just chops it into pieces. In Chapter 2, we'll build a parser that takes these tokens and builds a structure (an Abstract Syntax Tree) that represents what the code actually *means*.

## Need Help?

Stuck? Questions? Errors?

- **Issues:** [GitHub Issues](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [GitHub Discussions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)
- **Contribute:** Pull requests welcome!

Include:
- Chapter number
- Your OS/platform
- Full error message
- What you tried

Let's build this thing together.
