# 1. Pyxc: Introduction and the Lexer

## The Pyxc Language

This tutorial is illustrated with a language called "Pyxc" (pronounced "Pixie"). Pyxc is a procedural language that allows you to define functions, use conditionals, math, etc. Over the course of the tutorial, we’ll extend pyxc to support the if/then/else construct, a for loop, user defined operators, JIT compilation with a simple command line interface, debug info, etc.

Beyond the core tutorial, pyxc is built with a real compiler architecture in mind: a proper front end, a typed IR pipeline, optimization passes, code generation, and tooling hooks you would expect in serious language projects. As we move forward, we will add structs, classes, and richer type semantics, then push into advanced infrastructure like MLIR to model multi-level lowering and reusable compiler components. The aim is to use a readable, Python-like syntax as the surface language while implementing the same foundations used by production compilers.

We want to keep things simple at the start, so the only datatype in pyxc is a 64-bit floating point type (aka ‘double’ in C parlance). As such, all values are implicitly double precision and the language doesn’t require type declarations. This gives the language a very nice and simple syntax. For example, the following simple example computes [Fibonacci numbers](http://en.wikipedia.org/wiki/Fibonacci_number):

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

To keep things REALLY simple, we don't enforce indentation rules at the onset. What this means is that you could also write the above as:

```python
def fib(n): if n < 2: return n else: return fib(n-1) + fib(n-2)

fib(10)
```
For now, indentation is optional to keep the parser small and easy to understand; once the core pipeline is in place, later chapters will make indentation significant and progressively introduce richer language features.

We also allow pyxc to call into standard library functions - the LLVM JIT makes this really easy. This means that you can use the ‘extern’ keyword to define a function before you use it (this is also useful for mutually recursive functions). For example:

```python
extern def sin(arg)
extern def cos(arg)
extern def atan2(arg1 arg2)

atan2(sin(.4), cos(42))
```

A more interesting example is included in [Chapter 7](chapter-07.md) where we write a little pyxc application that displays a Mandelbrot Set at various levels of magnification.

Let’s dive into the implementation of this language!

## Source Code

To follow along you can download the code from GitHub [pyxc-llvm-tutorial](https://github.com/alankarmisra/pyxc-llvm-tutorial) or you can see the full source code here [code/chapter01](https://github.com/alankarmisra/pyxc-llvm-tutorial/tree/main/code/chapter01).


## The Lexer

When it comes to implementing a language, the first thing needed is the ability to process a text file and recognize what it says. The traditional way to do this is to use a [lexer](https://en.wikipedia.org/wiki/Lexical_analysis) (aka *scanner*) to break the input up into *tokens*. Each token returned by the lexer includes a token code and potentially some metadata (e.g. the numeric value of a number). First, we define the possibilities:

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

Each token returned by our lexer will either be one of the Token enum values or it will be an ‘unknown’ character like ‘+’, which is returned as its ASCII value. If the current token is an identifier, the IdentifierStr global variable holds the name of the identifier. If the current token is a numeric literal (like 1.0), NumVal holds its value. We use global variables for simplicity, but this is not the best choice for a real language implementation :).

Before we look at `gettok()`, we add two tiny location-tracking structures and one helper:

```cpp
struct SourceLocation {
  int Line;
  int Col;
};
static SourceLocation CurLoc;
static SourceLocation LexLoc = {1, 0};

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

`LexLoc` tracks where the lexer is currently reading. `CurLoc` is the start location of the current token. The `advance()` helper centralizes character reading and location updates so `gettok()` does not need to manually keep line/column counters in every branch. It also normalizes Windows `\r\n` into one logical newline by peeking one character ahead and pushing non-`\n` characters back with `ungetc`.

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
This is all pretty straightforward code for processing input. When reading a numeric value from input, we use the C `strtod` function to convert it to a numeric value that we store in NumVal. Note that this isn’t doing sufficient error checking: it will incorrectly read “1.23.45.67” and handle it as if you typed in “1.23”. Feel free to extend it! Next we handle comments:

```cpp
if (LastChar == '#') {
    // Comment until end of line.
    do
      LastChar = advance();
    while (LastChar != EOF && LastChar != '\n');

    if (LastChar != EOF)
      return tok_eol;
}
```

We handle comments by skipping to the end of the line and then return the end of line token. Finally, if the input doesn’t match one of the above cases, it is either an operator character like ‘+’ or the end of the file. These are handled with this code:

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

    printf("<%s>", GetTokenName(Tok).c_str());

    if (Tok == tok_eol)
      printf("\nready> ");
  }
}

int main() {
  MainLoop();
  return 0;
}
```

This is not a full interpreter yet. It is only a quick feedback loop to verify that the lexer tokenizes input the way we expect.

## Sample interaction

```python
$ build/pyxc

ready> def foo(x):
<tok_def><tok_identifier><tok_char, '('><tok_identifier><tok_char, ')'><tok_char, ':'><tok_eol>
ready> return x + 1
<tok_return><tok_identifier><tok_char, '+'><tok_number><tok_eol>
ready> extern def add(a,b)
<tok_extern><tok_def><tok_identifier><tok_char, '('><tok_identifier><tok_char, ','><tok_identifier><tok_char, ')'><tok_eol>
ready> ! # comment after a character token
<tok_char, '!'><tok_eol>
ready> <tok_eol>
ready> ^D
```

## Building
We will use [CMake](https://cmake.org/) for our builds so that when we have an explosion of LLVM related switches, we can look at each other with positive approval for having the insight to use CMake early on.

```bash
# build.sh

# Configure step:
# -S .   -> source dir is current directory (where CMakeLists.txt lives)
# -B build -> generate build files in ./build
cmake -S . -B build

# Build step:
# --build build -> compile using the generated build system in ./build
cmake --build build
```

If you are running a Unix-y OS (including MacOS), you can just run
```bash
./build.sh
```

## Testing

You can run tests with `llvm-lit` if you have an LLVM build available on your machine, or with `lit` if you installed it through Python. If you do not have either yet, that is okay: running tests is optional at this stage, and the interaction samples above are enough to continue. We will cover LLVM setup in [Chapter 3](chapter-03.md).


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
