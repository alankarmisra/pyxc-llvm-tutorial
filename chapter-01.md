---
description: "Build your first lexer: break source code into tokens and see them print in real-time."
---
# 1. Pyxc: Lexer Basics

## What We're Building

We're building a programming language called Pyxc (pronounced "Pixie"). Think Python syntax, but simpler—at least to start.

Here's a taste of what Pyxc code looks like:

```python
def fib(n):
    if n < 2:
        return n
    else:
        return fib(n-1) + fib(n-2)

fib(10)
```

You can also smash it onto one line (we're flexible about formatting early on):

```python
def fib(n): if n < 2: return n else: return fib(n-1) + fib(n-2)
```

We start simple: only one type (`double`), no type declarations, no complicated syntax. Just functions, math, and calling other functions.

You can even call C library functions:

```python
extern def sin(x)
extern def cos(x)

sin(1.0) + cos(2.0)
```

That `extern` tells Pyxc "this function exists somewhere else, just trust me." LLVM handles the rest.

## Source Code

Grab the code: [code/chapter01](https://github.com/alankarmisra/pyxc-llvm-tutorial/tree/main/code/chapter01)

Or clone the whole repo:
```bash
git clone https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter01
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

  tok_def = -3,        // 'def' keyword
  tok_extern = -4,     // 'extern' keyword

  tok_identifier = -5, // Variable/function names
  tok_number = -6,     // Numbers like 3.14

  tok_return = -7      // 'return' keyword
};
```

Why negative numbers? Because we also return single-character tokens (like `+`, `(`, `)`) as their ASCII values (which are positive). This way, we don't collide.

Example:
- `tok_def` → -3
- `+` → 43 (its ASCII code)
- `(` → 40

We also have two globals to hold extra info:

```cpp
static string IdentifierStr;  // Holds the name (if it's an identifier)
static double NumVal;          // Holds the value (if it's a number)
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

Now we can print `'def'` instead of `-3`. Much easier to read.

## Tracking Source Locations

We track where we are in the file (line and column):

```cpp
struct SourceLocation {
  int Line;
  int Col;
};

static SourceLocation CurLoc;    // Start of current token
static SourceLocation LexLoc = {1, 0};  // Current read position
```

Why track this? So error messages can say "Error on line 5, column 12" instead of just "Error."

We also save the source text as we read it:

```cpp
class SourceManager {
  vector<string> CompletedLines;
  string CurrentLine;

public:
  void onChar(int C) {
    if (C == '\n') {
      CompletedLines.push_back(CurrentLine);
      CurrentLine.clear();
    } else if (C != EOF) {
      CurrentLine.push_back((char)C);
    }
  }

  const string *getLine(int LineNumber) const {
    // Returns the Nth line of source code
    // ...
  }
};

static SourceManager DiagSourceMgr;
```

Every character we read gets logged here. Later, we can show the exact line where an error occurred.

## Reading Characters

Instead of calling `getchar()` directly, we wrap it:

```cpp
static int advance() {
  int LastChar = getchar();

  // Normalize Windows line endings (\r\n → \n)
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
3. Normalizes line endings (so `\r\n` becomes `\n`)

Now `gettok()` can just call `advance()` and everything is tracked automatically.

## The Main Lexer: `gettok()`

Here's where the magic happens. This function reads characters and returns tokens:

```cpp
static int gettok() {
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

Newlines are significant—they end statements.

### Recognizing Identifiers and Keywords

```cpp
  if (isalpha(LastChar) || LastChar == '_') {
    IdentifierStr = LastChar;
    while (isalnum((LastChar = advance())) || LastChar == '_')
      IdentifierStr += LastChar;

    // Check if it's a keyword
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

    NumVal = strtod(NumStr.c_str(), nullptr);
    return tok_number;
  }
```

We grab all digits and decimal points, then convert to a `double`.

**Note:** This is sloppy—it accepts `1.2.3.4` as valid. We'll fix that later if needed.

Examples:
- `42` → `tok_number` (and `NumVal = 42.0`)
- `3.14` → `tok_number` (and `NumVal = 3.14`)
- `.5` → `tok_number` (and `NumVal = 0.5`)

### Recognizing Comments

```cpp
  if (LastChar == '#') {
    do
      LastChar = advance();
    while (LastChar != EOF && LastChar != '\n');

    if (LastChar != EOF) {
      LastChar = ' ';  // Don't emit a second newline token
      return tok_eol;
    }
  }
```

Comments run from `#` to the end of the line. We skip them and return a single `tok_eol`.

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

Try it:

```bash
cd code/chapter01
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && cmake --build build
./build/pyxc
```

Type some code:

```
ready> def foo(x):
'def' identifier '(' identifier ')' ':' newline
ready> return x + 1
'return' identifier '+' number newline
ready> # This is a comment
newline
ready> ^D
```

The lexer breaks everything into tokens. Success!

## What We Built

- **Token types** - Enum for keywords, identifiers, numbers, etc.
- **`gettok()`** - Main lexer function
- **Source tracking** - Line/column info for error messages
- **Simple REPL** - See tokens in real-time

## What's Next

The lexer doesn't *understand* code—it just chops it into pieces. In Chapter 2, we'll build a parser that takes these tokens and builds a structure (an Abstract Syntax Tree) that represents what the code actually *means*.

## Compile and Run

```bash
cd code/chapter01
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
ready> ^D
```

Notice:
- Keywords like `def` show up as `'def'`
- Names like `add` show up as `identifier`
- Operators like `+` show up as `'+'`
- Newlines show up as `newline`

The lexer is working!

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
