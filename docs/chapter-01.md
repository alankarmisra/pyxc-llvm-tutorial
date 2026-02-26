---
description: "Build your first lexer: break source code into tokens and see them print in real-time."
---
# 1. Pyxc: The Lexer

## What We're Building

By the end of this tutorial, you'll have built a compiler for a statically-typed, Python-like language. Here's the kind of code it will eventually compile:

```python
def fibonacci(n: int) -> int:
    if n <= 1:
        return n
    return fibonacci(n - 1) + fibonacci(n - 2)
```

This chapter focuses on the very first piece: the **lexer**. Its job is to chop raw source text into tokens — the smallest meaningful units of a language.

For now, the language is deliberately simple. No types, no control flow yet. Just functions, math, and recursion:

```python
def fib(n):
    return fib(n-1) + fib(n-2)
```

You can even call C library functions by declaring them with `extern`:

```python
extern def sin(x)
extern def cos(x)

sin(1.0) + cos(2.0)
```

That `extern` tells Pyxc "this function lives in a C library — trust me." LLVM handles the wiring.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-01
```

## What's a Lexer?

A lexer reads raw text and breaks it into **tokens** — labeled pieces the parser can work with.

**Input:**
```python
def add(x, y):
    return x + y
```

**Output:**
```
'def' identifier '(' identifier ',' identifier ')' ':' newline
'return' identifier '+' identifier newline
```

Each word or symbol becomes a token. The lexer doesn't understand what `def` *means* — it just recognizes "this is the keyword `def`." That understanding is the parser's job, which comes next chapter.

## Token Types

We define an enum for the token types we need:

```cpp
enum Token {
  tok_eof = -1,        // End of file
  tok_eol = -2,        // End of line ('\n')

  tok_def     = -3,    // 'def' keyword
  tok_extern  = -4,    // 'extern' keyword
  tok_return  = -5,    // 'return' keyword

  tok_identifier = -6, // Variable/function names like foo, my_var
  tok_number     = -7, // Numbers like 42 or 3.14
};
```

Why negative numbers? Because single-character tokens like `+`, `(`, and `)` are returned as their ASCII values — which are all positive. Using negatives for named tokens means the two ranges never collide.

For example:
- `tok_def` → -3
- `+` → 43 (ASCII)
- `(` → 40 (ASCII)

We also need two global variables to carry extra information alongside a token:

```cpp
static std::string IdentifierStr; // Set when tok_identifier is returned
static double NumVal;             // Set when tok_number is returned
```

When the lexer sees `foo`, it returns `tok_identifier` and sets `IdentifierStr = "foo"`.
When it sees `3.14`, it returns `tok_number` and sets `NumVal = 3.14`.

## Reading Characters

Instead of calling `getchar()` directly everywhere, we wrap it in a small helper that also normalizes Windows (`\r\n`) and old Mac (`\r`) line endings to plain `\n`:

```cpp
static int advance() {
  int LastChar = getchar();

  // Normalize \r\n (Windows) and bare \r (old Mac) to \n
  if (LastChar == '\r') {
    int NextChar = getchar();
    if (NextChar != '\n' && NextChar != EOF)
      ungetc(NextChar, stdin);
    return '\n';
  }

  return LastChar;
}
```

This is a one-time fix that keeps the rest of the lexer clean — it never has to think about platform-specific line endings again.

## The Main Lexer: `gettok()`

This is the heart of the chapter. `gettok()` reads characters and returns the next token:

```cpp
static int gettok() {
  static int LastChar = ' ';
```

The `static` means `LastChar` persists between calls — it holds the last character we read but haven't processed yet (the lookahead). We initialize it to `' '` so the whitespace-skip loop below runs immediately on the first call.

### Skip Whitespace (But Not Newlines)

```cpp
  while (isspace(LastChar) && LastChar != '\n')
    LastChar = advance();
```

We skip spaces and tabs, but deliberately keep newlines — in a Python-like language, newlines end statements and matter to the parser.

### Newlines

```cpp
  if (LastChar == '\n') {
    LastChar = ' ';
    return tok_eol;
  }
```

When we see a newline, we return `tok_eol` immediately and reset `LastChar` to `' '`.

Notice we **don't** call `advance()` here. Every other token handler reads one character *past* the token to set up the next call. Newlines deliberately skip that step.

Why? Because `advance()` calls `getchar()`, which blocks waiting for input. If we consumed a character after seeing `\n`, the REPL would freeze waiting for the *next* line before it could show its prompt. By just setting `LastChar = ' '` and returning, the next call to `gettok()` will run the whitespace-skip loop, which will call `advance()` only when it's actually time to read something.

### Identifiers and Keywords

```cpp
  if (isalpha(LastChar) || LastChar == '_') {
    IdentifierStr = LastChar;
    while (isalnum(LastChar = advance()) || LastChar == '_')
      IdentifierStr += LastChar;

    static std::map<std::string, Token> Keywords = {
        {"def",    tok_def},
        {"extern", tok_extern},
        {"return", tok_return},
    };

    auto It = Keywords.find(IdentifierStr);
    return (It != Keywords.end()) ? It->second : tok_identifier;
  }
```

We accumulate letters, digits, and underscores into `IdentifierStr`, then check if it's a keyword. If it matches, we return the keyword token. If not, we return `tok_identifier` (and the caller reads `IdentifierStr` to get the name).

Examples:
- `def` → `tok_def`
- `foo` → `tok_identifier`, `IdentifierStr = "foo"`
- `my_var` → `tok_identifier`, `IdentifierStr = "my_var"`

### Numbers

```cpp
  if (isdigit(LastChar) || LastChar == '.') {
    std::string NumStr;
    do {
      NumStr += LastChar;
      LastChar = advance();
    } while (isdigit(LastChar) || LastChar == '.');

    char *End;
    NumVal = strtod(NumStr.c_str(), &End);
    if (*End != '\0') {
      // strtod stopped early — something wasn't a valid digit or decimal point
      fprintf(stderr, "Error: invalid number literal '%s'\n", NumStr.c_str());
      return tok_eol; // skip the rest of this line
    }
    return tok_number;
  }
```

We collect all digits and dots into `NumStr`, then convert with `strtod`.

`strtod` sets its second argument to point at the first character it couldn't parse. If that's not the end of the string (`*End != '\0'`), the number was malformed — for example, `1.2.3` would have `End` pointing at `.3`. We print an error and skip to the next line rather than silently returning a wrong value.

Examples:
- `42` → `tok_number`, `NumVal = 42.0`
- `3.14` → `tok_number`, `NumVal = 3.14`
- `.5` → `tok_number`, `NumVal = 0.5`
- `1.2.3` → error message, `tok_eol`

### Comments

```cpp
  if (LastChar == '#') {
    do
      LastChar = advance();
    while (LastChar != EOF && LastChar != '\n');

    if (LastChar != EOF) {
      LastChar = ' '; // same reason as newline: don't block the REPL
      return tok_eol;
    }
  }
```

Comments run from `#` to the end of the line. We discard them and return `tok_eol`, as if the comment were a newline. (If a comment runs all the way to EOF with no trailing newline, we fall through to the EOF case below.)

### End of File and Single Characters

```cpp
  if (LastChar == EOF)
    return tok_eof;

  int ThisChar = LastChar;
  LastChar = advance();
  return ThisChar;
}
```

If it's not a keyword, identifier, number, or comment, it's a single character like `+`, `(`, or `:`. We return its ASCII value directly, which the parser will compare against character literals.

## Testing the Lexer

We build a token-printing loop so we can see the lexer working before we build any parser:

```cpp
static void MainLoop() {
  fprintf(stderr, "ready> ");
  while (true) {
    int Tok = gettok();
    if (Tok == tok_eof)
      break;

    // Print the token
    switch (Tok) {
    case tok_eol:        fprintf(stderr, "newline\nready> "); break;
    case tok_def:        fprintf(stderr, "'def' "); break;
    case tok_extern:     fprintf(stderr, "'extern' "); break;
    case tok_return:     fprintf(stderr, "'return' "); break;
    case tok_identifier: fprintf(stderr, "identifier "); break;
    case tok_number:     fprintf(stderr, "number "); break;
    default:             fprintf(stderr, "'%c' ", Tok); break;
    }
  }
}

int main() {
  MainLoop();
  return 0;
}
```

## Build and Run

```bash
cd code/chapter-01
cmake -S . -B build && cmake --build build
./build/pyxc
```

Or use the provided script:
```bash
./build.sh
```

## Try It

```
ready> def add(a, b):
'def' identifier '(' identifier ',' identifier ')' ':' newline
ready> return a + b
'return' identifier '+' identifier newline
ready> extern def sin(x)
'extern' 'def' identifier '(' identifier ')' newline
ready> 1.2.3 + 4
Error: invalid number literal '1.2.3'
newline
ready> ^D
```

Each token appears as we'd expect. Keywords show as `'def'`, names as `identifier`, operators as `'+'`.

Notice that `1.2.3` produces an error message and skips to the next line — but the `+ 4` on the same line is lost. That's fine for now. In Chapter 2, once the parser is in place, we'll have a proper place to do error recovery.

## What We Built

| Piece | What it does |
|---|---|
| `enum Token` | Names for the different kinds of tokens |
| `IdentifierStr`, `NumVal` | Carry extra data alongside a token |
| `advance()` | Read one character, normalize line endings |
| `gettok()` | The main lexer — returns the next token |
| `MainLoop()` | Print each token so we can see the lexer working |

## What's Next

The lexer chops source code into pieces but doesn't understand what they mean. In Chapter 2, we'll build a **parser** that reads these tokens and constructs an **Abstract Syntax Tree** (AST) — a tree structure that captures the actual meaning of the code. That's when the language starts to take shape.
