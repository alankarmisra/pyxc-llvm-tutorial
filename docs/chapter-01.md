---
description: "Teach the compiler to read: break source text into tokens the parser can work with."
---
# 1. Pyxc: Reading Source Code

## What We're Building

By the end of this tutorial, you'll have built a compiler for a Python-like language. Here's what we're eventually aiming for:

```python
def fibonacci(n: int) -> int:
    if n <= 1:
        return n
    return fibonacci(n - 1) + fibonacci(n - 2)
```

The very first thing a compiler needs to do is read source text and classify each piece — is this a number? A function name? A `+` sign? A keyword like `def`? Right now the compiler can't even do that. This chapter adds it.

For now we keep the language small — no `if` conditionals or `for` loops yet.

```python
def add(a, b):
    return a + b
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-01
```

## Breaking Source Text Into Tokens

Source code is just a string of characters. The compiler needs to classify each meaningful chunk before it can do anything useful — is `def` a keyword? Is `add` a function name? Is `+` an operator? Is `3.14` a number?

The first step is to scan the string and group characters into classified chunks:

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

The scanning step is called **lexing** (from Latin *lexis*, meaning word). If you've ever split a string to pull out numbers and punctuation, you've done it manually. Here we write it properly, handling keywords, operators, names, and numbers in one pass.

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
  // Check for newline.
  if (LastChar == '\n') {
    // Don't try and read the next character. This will stall the REPL.
    // Just reset LastChar to a space which will force a new character
    // advance in the next call.
    LastChar = ' ';
    return tok_eol;
  }
```

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

The if-chain is simple and clear for three keywords. In a later chapter, when we add more keywords (like `if`, `else`, `while`, `let`), we'll move this into a map. The code even has a note reminding us:

```cpp
// TODO: Push this into a map
```

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

But notice the code has a comment:

```cpp
// TODO: This incorrectly lexes 1.23.45.67 as 1.23
```

`strtod` parses as far as it can and stops at the second `.`. The rest of `1.23.45.67` — the `.45.67` part — then becomes a surprise for the parser on the next call. We'll fix this in a later chapter by checking where `strtod` stopped and erroring on leftover junk. For now, we leave it as-is — it's not a problem for valid input.

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

Comments run from `#` to the end of the line. We discard all of it and return `tok_eol` as if the comment were a blank line.

One subtle point: we set `LastChar = ' '` instead of calling `advance()`. If we called `advance()` here, the REPL would block — `getchar()` waits for the next line of input before it can return `tok_eol` and show a fresh prompt. Setting `LastChar = ' '` lets us return immediately, and the whitespace-skip loop at the top of the next call will trigger `advance()` only when it's actually time to read again.

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

## What's Missing (On Purpose)

This file has no `main()` and no `MainLoop()`. It's purely the lexer — just the `Token` enum, two globals, `advance()`, and `gettok()`. In the next chapter we'll add the parser alongside a simple driver that calls `gettok()` and prints what it sees. Keeping this file focused means you can read the entire lexer in one sitting.

## What We Built

| Piece | What it does |
|---|---|
| `enum Token` | Names for each kind of piece (keyword, number, identifier, symbol) |
| `IdentifierStr`, `NumVal` | The actual text or value that came with a token |
| `advance()` | Reads one character, normalizes line endings |
| `gettok()` | Reads characters and returns the next token — the lexer |

Two deliberate TODOs carried forward to the next chapter:
- The keyword if-chain becomes a map when we add more keywords
- Malformed numbers like `1.2.3` get proper error handling once the parser is in place

## What's Next

We can now split source text into pieces, but we don't understand what those pieces mean yet. In [Chapter 2](chapter-02.md) we'll build something that reads the token stream and works out the structure — that `def add(x, y)` is a function taking two arguments, that `x + y` is an addition. We'll also add `main()` so we can actually run something for the first time.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version` and `ninja --version`

We'll figure it out.
