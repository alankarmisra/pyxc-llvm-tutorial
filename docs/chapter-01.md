---
description: "Teach the compiler to read: break source text into tokens the parser can work with."
---
# 1. Pyxc: The First Pass

## Where We're Headed

By the end of this tutorial, you'll have built a compiler for a Python-like language that compiles to native code and runs directly on your CPU — no interpreter overhead.

In the initial chapters, we keep the language small and simple — no `if` conditionals or `for` loops, and only 64-bit floating point types. We will introduce more sophisticated patterns little by little. Here's what we are going to build over the next few chapters:

```python
# test.pyxc
extern def sin(x)    # pull in a C-standard library function (yes THAT C)
extern def cos(x)    # and another
extern def printd(x) # a custom pyxc library function

# define your own function
def identity(x): # returns double by default
    return sin(x) * sin(x) + cos(x) * cos(x)

printd(identity(4)) # function calls
```

By chapter 6, running this file will output:

```bash
1.000000
```

## Where We Are

There is no compiler yet — just a blank C++ file. By the end of this chapter, the lexer can read source text and classify every piece of it into tokens. That's what we're building.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-01
```

## Breaking Source Text Into Tokens

The very first thing a compiler needs to do is read source text and classify each piece — is this a number? A function name? A `+` sign? A keyword like `def`? Let's add this first.  We begin by scanning the source text and grouping characters into classified chunks:

**Input:**
```python
def add(x, y):
    return x + y
```

**Output:**
```
keyword:'def'  name:'add'  '('  name:'x'  ','  name:'y'  ')'  ':'  newline
keyword:'return'  name:'x'  '+'  name:'y'  newline
```

Each chunk is one **token** — a classified piece of source text. The code doing this doesn't understand what `def` *means* yet — it just recognises *"this is the keyword `def`."* Figuring out what it means is the next chapter's job.

The scanning step is called **lexing** (from Latin *lexis*, meaning word). 

## Token Types

We define an enum for the kinds of tokens we need:

```cpp
enum Token {
  tok_eof = -1,  // End of file
  tok_eol = -2,  // End of line ('\n')

  tok_def    = -3,  // 'def' keyword
  tok_extern = -4,  // 'extern' keyword

  tok_identifier = -5, // Variable/function names: foo, my_var
  tok_number     = -6, // Numbers: 42, 3.14

  tok_return = -7 // 'return' keyword
};
```

Why negative numbers? Because single-character tokens like `+`, `(`, and `)` are returned directly as their ASCII values, which are all positive. Negative values for named tokens means the two ranges never collide.

For example:
- `tok_def` → -3
- `+` → 43 (ASCII)
- `(` → 40 (ASCII)

We also need two global variables to carry extra data alongside a token return value:

```cpp
static string IdentifierStr; // Filled in if tok_identifier
static double NumVal;        // Filled in if tok_number
```

When the lexer sees the name `foo`, it returns `tok_identifier` and sets `IdentifierStr = "foo"`.
When it sees `3.14`, it returns `tok_number` and sets `NumVal = 3.14`.

## Reading Characters

Instead of calling `getchar()` directly throughout the lexer, we wrap it in a helper:

```cpp
int advance() {
  int LastChar = getchar();

  // Coalesce \r\n (Windows) into \n, convert bare \r (old Mac) to \n
  if (LastChar == '\r') {
    int NextChar = getchar();
    if (NextChar != '\n' && NextChar != EOF)
      ungetc(NextChar, stdin);
    return '\n';
  }

  return LastChar;
}
```

This normalizes line endings across platforms: Windows sends `\r\n`, old Macs send bare `\r`. We collapse both to `\n` once here so the rest of the lexer never has to think about it again.

## gettok(): Reading One Token at a Time

This is the heart of the chapter. Every time the compiler needs the next piece of source text, it calls `gettok()`. It reads characters and returns a token.

```cpp
int gettok() {
  static int LastChar = ' ';
```

`LastChar` is `static`, so it persists between calls. It holds the last character read that hasn't been consumed yet — the one-character lookahead every lexer needs. We initialize it to `' '` (a space) so the whitespace-skipping loop below runs immediately on the first call, which reads the actual first character of input.

### Skip Whitespace (But Not Newlines)

```cpp
  while (isspace(LastChar) && LastChar != '\n')
    LastChar = advance();
```

We skip spaces and tabs, but keep newlines. In a Python-like language, newlines end statements — they're meaningful to the parser. So we preserve them.

### Newlines

```cpp
  if (LastChar == '\n') {
    LastChar = ' ';
    return tok_eol;
  }
```

We don't call `advance()` here. If we did, the REPL would stall — `getchar()` waits for the next line of input before returning, so the prompt would freeze after every newline. Setting `LastChar = ' '` lets us return `tok_eol` immediately; the whitespace-skip loop at the top of the next `gettok()` call will read the next character when it's actually needed.

### Identifiers and Keywords

```cpp
  if (isalpha(LastChar) || LastChar == '_') {
    IdentifierStr = LastChar;
    while (isalnum(LastChar = advance()) || LastChar == '_')
      IdentifierStr += LastChar;

    if (IdentifierStr == "def")    return tok_def;
    if (IdentifierStr == "extern") return tok_extern;
    if (IdentifierStr == "return") return tok_return;

    return tok_identifier;
  }
```

We accumulate letters, digits, and underscores into `IdentifierStr`, then check if it's a keyword. If it matches `"def"`, `"extern"`, or `"return"`, we return the appropriate keyword token. Otherwise it's a plain identifier.

Examples:
- `def` → `tok_def`
- `foo` → `tok_identifier`, `IdentifierStr = "foo"`
- `my_var` → `tok_identifier`, `IdentifierStr = "my_var"`

The if-chain is simple and clear for three keywords. In a later chapter, when we add more keywords (like `if`, `else`, `while`, `let`), we'll move this into a map. For now, we leave a TODO in the code so we remember to do it later.

### Numbers

```cpp
  if (isdigit(LastChar) || LastChar == '.') {
    string NumStr;
    do {
      NumStr += LastChar;
      LastChar = advance();
    } while (isdigit(LastChar) || LastChar == '.');

    NumVal = strtod(NumStr.c_str(), 0);
    return tok_number;
  }
```

We accumulate all digits and dots into `NumStr`, then convert to a `double` using `strtod`. The result goes into `NumVal`.

This works correctly for normal inputs:
- `42` → `tok_number`, `NumVal = 42.0`
- `3.14` → `tok_number`, `NumVal = 3.14`
- `.5` → `tok_number`, `NumVal = 0.5`

There is a subtle bug though. `strtod` parses as far as it can and stops at the second `.`. The rest of `1.23.45.67` — the `.45.67` part — is effectively ignored. We'll fix this in a later chapter by checking where `strtod` stopped and erroring on leftover junk. For now, we leave a TODO in the code and move on — it's not a problem for valid input.

### Comments

```cpp
  if (LastChar == '#') {
    do {
      LastChar = advance();
    } while (LastChar != '\n' && LastChar != EOF);

    if (LastChar != EOF) {
      LastChar = ' ';
      return tok_eol;
    }
  }
```

Comments run from `#` to the end of the line. We discard all of it and return `tok_eol` as if the comment were a blank line. The parser will never know the comment existed.

The `LastChar = ' '` trick is the same as in the Newlines case — return immediately rather than blocking the REPL.

If the comment runs all the way to `EOF` with no newline, `LastChar` is `EOF` and we fall through to the EOF case below.

### End Of File
```cpp
  if (LastChar == EOF)
    return tok_eof;
```

### Everything Else

```cpp
  int ThisChar = LastChar;
  LastChar = advance();
  return ThisChar;
}
```

If we haven't matched anything else, it's a single character — `+`, `(`, `:`, etc. We return its ASCII value directly. The parser will compare against character literals like `'+'` or `'('`.

## The Driver

To run the lexer we need two things: a way to print what each token is, and a `main()` that drives the loop.

`TokenNames` maps each named token to a readable string:

```cpp
static map<int, string> TokenNames = {
    {tok_eof,        "tok_eof"},
    {tok_eol,        "tok_eol"},
    {tok_def,        "tok_def"},
    {tok_extern,     "tok_extern"},
    {tok_identifier, "tok_identifier"},
    {tok_number,     "tok_number"},
    {tok_return,     "tok_return"},
};
```

In Chapter 3, `TokenNames` grows to cover every possible token — including single-character ones like `+` and `(` — with friendlier names for error messages.

`main()` calls `gettok()` in a loop and prints each token:

```cpp
int main() {
  int tok;
  while ((tok = gettok()) != tok_eof) {
    if (tok == tok_identifier)
      fprintf(stdout, "tok_identifier: %s\n", IdentifierStr.c_str());
    else if (tok == tok_number)
      fprintf(stdout, "tok_number: %g\n", NumVal);
    else if (tok < 0)
      fprintf(stdout, "%s\n", TokenNames[tok].c_str());
    else
      fprintf(stdout, "'%c'\n", (char)tok);
  }
  return 0;
}
```

`tok_identifier` and `tok_number` are handled separately because the token integer alone isn't enough — the associated payload (`IdentifierStr` and `NumVal`) needs to be printed too. All other named tokens go through `TokenNames`. Single-character tokens (positive ASCII values) print as `'c'`.

## Build and Run

```bash
cd code/chapter-01
cmake -S . -B build && cmake --build build
printf "def add(x, y):\n    return x + y\n" | ./build/pyxc
```

```
tok_def
tok_identifier: add
'('
tok_identifier: x
','
tok_identifier: y
')'
':'
tok_eol
tok_return
tok_identifier: x
'+'
tok_identifier: y
tok_eol
```

The `test/` directory has lit tests covering each token type — one file per rule. Browse them for more input examples, or run the suite:

```bash
llvm-lit code/chapter-01/test/
```

## What's Next

In [Chapter 2](chapter-02.md) we build the parser on top of the lexer. The parser reads the token stream and works out the structure — that `def add(x, y)` is a function taking two arguments, that `x + y` is an addition.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version` and `ninja --version`

We'll figure it out.
