---
description: "Build the first working lexer for Pyxc: tokenize identifiers, numbers, keywords, operators, comments, and line endings, then inspect output in a simple REPL loop."
---
# 1. Pyxc: Introduction and the Lexer

## The Pyxc Language

This tutorial builds a language called "Pyxc" (pronounced "Pixie"). Pyxc is a procedural language with Python-like syntax. We'll start simple and progressively add features: functions, conditionals, loops, user-defined operators, JIT compilation, optimization, control flow, types, structs, and eventually a full compiler toolchain with debug info.

To keep things manageable early on, we start with just one datatype: 64-bit floating point (C's `double`). All values are implicitly doubles, so no type declarations needed. This keeps the syntax clean. For example, here's how you compute [Fibonacci numbers](http://en.wikipedia.org/wiki/Fibonacci_number):

```python
# Compute the x'th fibonacci number.
def fib(n):
    if n < 2:
        return n
    else:
        return fib(n-1) + fib(n-2)

# This expression will compute the 40th number.
fib(10)
```

You can also write everything on one line:

```python
def fib(n): if n < 2: return n else: return fib(n-1) + fib(n-2)

fib(10)
```

Indentation is optional for now—this keeps the parser simple. Later chapters will make indentation significant and add more language features.

Pyxc can call standard library functions—the LLVM JIT makes this straightforward. Use `extern` to declare a function before using it (also useful for mutually recursive functions):

```python
extern def sin(arg)
extern def cos(arg)
extern def atan2(arg1 arg2)

atan2(sin(.4), cos(42))
```

A more interesting example is included in [Chapter 9](chapter-09.md) where we write a little pyxc application that displays a Mandelbrot Set at various levels of magnification.

Let’s dive into the implementation of this language!

## Source Code

To follow along you can download the code from GitHub [pyxc-llvm-tutorial](https://github.com/alankarmisra/pyxc-llvm-tutorial) or you can see the full source code here [code/chapter01](https://github.com/alankarmisra/pyxc-llvm-tutorial/tree/main/code/chapter01).


## The Lexer

When implementing a language, the first step is processing a text file and recognizing its structure. A [lexer](https://en.wikipedia.org/wiki/Lexical_analysis) (aka *scanner*) breaks the input into *tokens*. Each token has a token code and optional metadata (e.g., the numeric value of a number). First, we define the token types:

```CPP
// The lexer returns tokens [0-255] if it is an unknown character, otherwise one
// of these for known things.
enum Token {
  tok_eof = -1,
  tok_eol = -2,

  // commands
  tok_def = -3,
  tok_extern = -4,

  // primary
  tok_identifier = -5,
  tok_number = -6,

  // control
  tok_return = -7
};

static string IdentifierStr; // Filled in if tok_identifier
static double NumVal;             // Filled in if tok_number
```

Each token returned by our lexer is either one of the `Token` enum values or an unknown character like `+`, which is returned as its ASCII value. If the current token is an identifier, `IdentifierStr` holds its name. If it's a numeric literal (like `1.0`), `NumVal` holds the value. We use global variables for simplicity—real language implementations use better encapsulation.

To make output easier to read (both in the REPL and in diagnostics in later chapters), we map tokens to user-friendly strings:

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

  for (int C = 0; C <= 255; ++C) {
    if (isprint(static_cast<unsigned char>(C)))
      Names[C] = "'" + string(1, static_cast<char>(C)) + "'";
    else if (C == '\n')
      Names[C] = "'\\n'";
    else if (C == '\t')
      Names[C] = "'\\t'";
    else if (C == '\r')
      Names[C] = "'\\r'";
    else if (C == '\0')
      Names[C] = "'\\0'";
    else {
      ostringstream OS;
      OS << "0x" << uppercase << hex << setw(2) << setfill('0') << C;
      Names[C] = OS.str();
    }
  }
  return Names;
}();
```

Before we look at `gettok()`, we add two location-tracking structs, a small `SourceManager`, and one helper:

```cpp
struct SourceLocation {
  int Line;
  int Col;
};
static SourceLocation CurLoc;
static SourceLocation LexLoc = {1, 0};
```

`LexLoc` tracks where the lexer is currently reading. `CurLoc` is the start location of the current token.

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

static SourceManager SourceMgr;
```

`SourceMgr` stores source lines as characters are read, which we use for line-aware diagnostics in later chapters. Every time `advance()` reads a character, it calls `SourceMgr.onChar(...)`; the source manager buffers characters into the current line and pushes completed lines into its internal vector when it sees `\n`.

```cpp
static int advance() {
  int LastChar = getchar();
  if (LastChar == '\r') {
    int NextChar = getchar();
    if (NextChar != '\n' && NextChar != EOF)
      ungetc(NextChar, stdin);
    SourceMgr.onChar('\n');
    LexLoc.Line++;
    LexLoc.Col = 0;
    return '\n';
  }
  if (LastChar == '\n') {
    SourceMgr.onChar('\n');
    LexLoc.Line++;
    LexLoc.Col = 0;
  } else {
    SourceMgr.onChar(LastChar);
    LexLoc.Col++;
  }
  return LastChar;
}
```

The `advance()` helper centralizes character reading and location updates so `gettok()` does not need to manually keep line/column counters in every branch. It also normalizes Windows `\r\n` into one logical newline by peeking one character ahead and pushing non-`\n` characters back with `ungetc`.

Now the actual implementation of the lexer is a single function named `gettok()`. It is called to return the next token from standard input. Its definition starts as:

```cpp
/// gettok - Return the next token from standard input.
static int gettok() {
  static int LastChar = ' ';

  // Skip whitespace EXCEPT newlines
  while (isspace(LastChar) && LastChar != '\n')
    LastChar = advance();
```

`gettok()` works by consuming one character at a time from standard input via `advance()`. It eats characters as it recognizes them and stores the last character read, but not yet processed, in `LastChar`. The first thing it does is ignore whitespace between tokens except new lines. This is accomplished with the loop above.

The next thing we do is recognize newlines. In pyxc, newlines are significant - they mark the end of a statement, similar to Python's REPL behavior. We'll discuss why this matters and how the parser handles it in [Chapter 2](chapter-02.md), but for now, just know that the lexer treats newlines as a distinct token (`tok_eol`) rather than ignoring them like other whitespace.

```cpp
  // Return end-of-line token.
  if (LastChar == '\n') {
    LastChar = ' ';
    return tok_eol;
  }

  CurLoc = LexLoc;
```

The next thing gettok needs to do is recognize identifiers and specific keywords like `def`. Pyxc does this with this simple loop:

```cpp
// Keywords words like `def`, `extern` and `return`. The lexer will return the
// associated Token. Additional language keywords can easily be added here.
static map<string, Token> Keywords = {
    {"def", tok_def}, {"extern", tok_extern}, {"return", tok_return}};

...
if (isalpha(LastChar) || LastChar == '_') { // identifier: [a-zA-Z_][a-zA-Z0-9_]*
  IdentifierStr = LastChar;
  while (isalnum((LastChar = advance())) || LastChar == '_')
    IdentifierStr += LastChar;

    // Is this a known keyword? If yes, return that.
    // Else it is an ordinary identifier.
    const auto It = Keywords.find(IdentifierStr);
    return (It != Keywords.end()) ? It->second : tok_identifier;
}
```
Note that this code sets the `IdentifierStr` global whenever it lexes an identifier. Also, since language keywords are matched by the same loop, we handle them here. Numeric values are similar:

```cpp
if (isdigit(LastChar) || LastChar == '.') {   // Number: [0-9.]+
  string NumStr;
  do {
    NumStr += LastChar;
    LastChar = advance();
  } while (isdigit(LastChar) || LastChar == '.');

  NumVal = strtod(NumStr.c_str(), nullptr);
  return tok_number;
}
```
This is all pretty straightforward code for processing input. When reading a numeric value from input, we use the C `strtod` function to convert it to a numeric value that we store in NumVal. Note that this isn't doing sufficient error checking: it will incorrectly read "1.23.45.67" as "1.23". Next we handle comments:

```cpp
if (LastChar == '#') {
  // Comment until end of line.
  do
    LastChar = advance();
  while (LastChar != EOF && LastChar != '\n');

  if (LastChar != EOF) {
    LastChar = ' '; // consume newline once (avoid double tok_eol)
    return tok_eol;
  }
}
```

We handle comments by skipping to the end of the line and returning a single `tok_eol`. Setting `LastChar = ' '` is important here: it prevents the newline that was just consumed by `advance()` from being emitted a second time on the next `gettok()` call. Finally, if the input doesn’t match one of the above cases, it is either an operator character like `+` or the end of the file. These are handled with this code:

```cpp
  // Check for end of file.  Don't eat the EOF.
  if (LastChar == EOF)
    return tok_eof;

  // Otherwise, just return the character as its ascii value.
  int ThisChar = LastChar;
  LastChar = advance();
  return ThisChar;
}
```

With this, we have the complete lexer for the basic pyxc language. Next we’ll build a simple parser that uses this to build an Abstract Syntax Tree. When we have that, we’ll include a driver so that you can use the lexer and parser together.

## Rudimentary REPL

To make the lexer easy to observe, we add a small REPL-style loop in `main()`. It reads one token at a time from standard input with `gettok()`, prints the token name, and prints a fresh `ready>` prompt whenever we hit `tok_eol`.

```cpp
/// A rudimentary REPL. Type things in, see them convert to tokens.
void MainLoop() {
  fprintf(stderr, "ready> ");
  while (true) {
    const int Tok = gettok();
    if (Tok == tok_eof)
      break;

    printf("%s", GetTokenName(Tok).c_str());
    if (Tok == tok_eol)
      printf("\nready> ");
    else
      printf(" ");
  }
}

int main() {
  SourceMgr.reset();
  MainLoop();
  return 0;
}
```

This is a quick feedback loop to verify that the lexer tokenizes input the way we expect.

## Compiling

```bash
cd code/chapter01 && \
    cmake -S . -B build && \
    cmake --build build
```

### macOS / Linux shortcut

```bash
cd code/chapter01 && ./build.sh
```

## Sample interaction

```python
$ build/pyxc

ready> def foo(x):
'def' identifier '(' identifier ')' ':' newline
ready> return x + 1
'return' identifier '+' number newline
ready> extern def add(a,b)
'extern' 'def' identifier '(' identifier ',' identifier ')' newline
ready> ! # comment after a character token
'!' newline
ready> # full-line comment
newline
ready> ^D
```

Notice above, that comments will not yield a token, but the newline token after it will.

## Testing

You can run tests with `llvm-lit` if you have an LLVM build available on your machine, or with `lit` if you installed it through Python. If you do not have either yet, that is okay: running tests is optional at this stage, and the interaction samples above are enough to continue. We will cover LLVM setup in [Chapter 3](chapter-03.md).

## Conclusion

In this chapter we built the first real piece of pyxc: a lexer that turns raw source text into meaningful tokens, tracks source locations, handles comments and mixed line endings, and gives us a simple REPL loop so we can see tokenization live as we type.

That gives us a solid base for everything that comes next. In the next chapter, we will build a parser and an AST on top of this lexer so pyxc can understand full expressions and function forms.

## Need Help?

Stuck on something? Have questions about this chapter? Found an error?

- **Open an issue:** [GitHub Issues](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues) - Report bugs, errors, or problems
- **Start a discussion:** [GitHub Discussions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions) - Ask questions, share tips, or discuss the tutorial
- **Contribute:** Found a typo? Have a better explanation? [Pull requests](https://github.com/alankarmisra/pyxc-llvm-tutorial/pulls) are welcome!

**When reporting issues, please include:**
- The chapter you're working on
- Your platform (e.g., macOS 14 M2, Ubuntu 24.04, Windows 11)
- The complete error message or unexpected behavior
- What you've already tried

The goal is to make this tutorial work smoothly for everyone. Your feedback helps improve it for the next person!
