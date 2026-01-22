# 1. Pyxc: Introduction and the Lexer

## The Pyxc Language

This tutorial is illustrated with a language called "Pyxc" (pronounced "Pixie"), short for "Python executable". Pyxc is a procedural language that allows you to define functions, use conditionals, math, etc. Over the course of the tutorial, we’ll extend pyxc to support the if/then/else construct, a for loop, user defined operators, JIT compilation with a simple command line interface, debug info, etc.

Beyond the core tutorial, pyxc is designed to grow into a more complete language with structs, classes, a type system, and eventually explore advanced compiler infrastructure like MLIR. The goal is to build something real — not just a toy — while keeping the approachable, Python-like syntax that developers already know.

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
Granted this can look confusing, so we will write our programs with the indentation, but knowing that, for now, this is not enforced. In later chapters, once you are more familiar with the code, we will enforce indentation rules and add other advanced features.

We also allow pyxc to call into standard library functions - the LLVM JIT makes this really easy. This means that you can use the ‘extern’ keyword to define a function before you use it (this is also useful for mutually recursive functions). For example:

```python
extern def sin(arg)
extern def cos(arg)
extern def atan2(arg1 arg2)

atan2(sin(.4), cos(42))
```

A more interesting example is included in Chapter 6 where we write a little pyxc application that displays a Mandelbrot Set at various levels of magnification.

In more advanced chapters, we will extend on the data types, add type checking, and enforce python indentation rules. 

Let’s dive into the implementation of this language!

## The Lexer

!!!note The full code listing for the Lexer is available at the end of the next chapter of the tutorial where we add the Parser to create some testable code. 

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

static std::string IdentifierStr; // Filled in if tok_identifier
static double NumVal;             // Filled in if tok_number
```

Each token returned by our lexer will either be one of the Token enum values or it will be an ‘unknown’ character like ‘+’, which is returned as its ASCII value. If the current token is an identifier, the IdentifierStr global variable holds the name of the identifier. If the current token is a numeric literal (like 1.0), NumVal holds its value. We use global variables for simplicity, but this is not the best choice for a real language implementation :).

The actual implementation of the lexer is a single function named gettok. The gettok function is called to return the next token from standard input. Its definition starts as:

```cpp
/// gettok - Return the next token from standard input.
static int gettok() {
  static int LastChar = ' ';

  // Skip whitespace EXCEPT newlines
  while (isspace(LastChar) && LastChar != '\n' && LastChar != '\r')
    LastChar = getchar();
```

gettok works by calling the C getchar() function to read characters one at a time from standard input. It eats them as it recognizes them and stores the last character read, but not processed, in LastChar. The first thing that it has to do is ignore whitespace between tokens except new lines. This is accomplished with the loop above.

The next thing we do is recognize newlines. In pyxc, newlines are significant - they mark the end of a statement, similar to Python's REPL behavior. We'll discuss why this matters and how the parser handles it in [Chapter 2](chapter-02.md), but for now, just know that the lexer treats newlines as a distinct token (`tok_eol`) rather than ignoring them like other whitespace.

```cpp
  // Return end-of-line token
  if (LastChar == '\n' || LastChar == '\r') {
    // Reset LastChar to a space instead of reading the next character.
    // If we called getchar() here, it would block waiting for input,
    // requiring the user to press Enter twice in the REPL.
    // Setting LastChar = ' ' avoids this blocking read.
    LastChar = ' ';
    return tok_eol;
  }
```

The next thing gettok needs to do is recognize identifiers and specific keywords like `def`. Pyxc does this with this simple loop:

```cpp
// Keywords words like `def`, `extern` and `return`. The lexer will return the
// associated Token. Additional language keywords can easily be added here.
static std::map<std::string, Token> Keywords = {
    {"def", tok_def}, {"extern", tok_extern}, {"return", tok_return}};

...
if (isalpha(LastChar)) { // identifier: [a-zA-Z][a-zA-Z0-9]*
  IdentifierStr = LastChar;
  while (isalnum((LastChar = getchar())))
    IdentifierStr += LastChar;

    // Is this a known keyword? If yes, return that.
    // Else it is an ordinary identifier.
    const auto it = Keywords.find(IdentifierStr);
    return (it != Keywords.end()) ? it->second : tok_identifier;
}
```
Note that this code sets the `IdentifierStr` global whenever it lexes an identifier. Also, since language keywords are matched by the same loop, we handle them here. Numeric values are similar:

```cpp
if (isdigit(LastChar) || LastChar == '.') {   // Number: [0-9.]+
  std::string NumStr;
  do {
    NumStr += LastChar;
    LastChar = getchar();
  } while (isdigit(LastChar) || LastChar == '.');

  NumVal = strtod(NumStr.c_str(), 0);
  return tok_number;
}
```
This is all pretty straightforward code for processing input. When reading a numeric value from input, we use the C `strtod` function to convert it to a numeric value that we store in NumVal. Note that this isn’t doing sufficient error checking: it will incorrectly read “1.23.45.67” and handle it as if you typed in “1.23”. Feel free to extend it! Next we handle comments:

```cpp
if (LastChar == '#') {
    // Comment until end of line.
    do
      LastChar = getchar();
    while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');

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
  LastChar = getchar();
  return ThisChar;
}
```

With this, we have the complete lexer for the basic pyxc language. Next we’ll build a simple parser that uses this to build an Abstract Syntax Tree. When we have that, we’ll include a driver so that you can use the lexer and parser together.

!!!note Reminder: The full code listing for the Lexer is available at the end of the next chapter of the tutorial where we add the Parser to create some testable code. 
