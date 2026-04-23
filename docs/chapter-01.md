---
description: "Let's start at the very beginning. A very good place to start."
---
# 1. Pyxc: Let's start at the very beginning. 

## A very good place to start.

Writing compilers isn't hard. It's just a lot of moving parts. So we, the programmer, do what we always do. We start small and build from there. If something doesn't make sense to us, we [discuss it](#need-help) with people.  

Let me show you what we're working towards as a first pass. 

```python
# test.pyxc
extern def sin(x)    # pull in a C-standard library function with extern(al) - free lunch
extern def cos(x)    # and another - more free lunch
extern def printd(x) # write a custom pyxc library function

# define your own function (wowza)
def identity(x): # returns float64 by default
    return sin(x) * sin(x) + cos(x) * cos(x)

printd(identity(4)) # call the function. 
```

By [chapter 6](chapter-06.md), running this file will output:

```bash
1.000000
```

6 chapters!? Yes. But don't be disheartened. We will define and accomplish many milestones along the way to keep the momentum going. You've already accomplished something by getting to this tutorial. But don't call it a day yet. 

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-01
```

## What does it all mean?

Say we write this code:

**Input:**
```python
def add(x,y):
    return x + y    
```

You, the  programmer, already know that `def` is a *keyword*, `add` is a *function name*, `x` and `y` are *parameters*, and so on. We also have a *comment* on the first line. Now let's help the compiler understand the same. In more concrete terms, the compiler should be able to read the above source text and produce something like:

```
keyword:'def'  identifier:'add'  '('  identifier:'x'  ','  identifier:'y'  ')'  ':'  newline
keyword:'return'  identifier:'x'  '+'  identifier:'y'  newline
```

Each individual element like keyword: 'def', identifier:'add' is called a token. Notice that we've used a *identifier* token for both *function names* and *parameter names*. Turns out, this is enough for understanding the intention of the source program in later phases. This step of breaking up the source into tokens is called **lexing** (from Latin *lexis*, meaning word). 

## Tokens

*Tokens* swap out the string for a single number. For example, *def* could be represented by -3 (I don't know why I picked -3, it's just some number). This makes equality comparisons quicker. An equality check like `"def" == "def"` takes 3 character comparisons. An equality check of -3 == -3 takes only one. Spread over many keywords across many source files, it can save us a fair bit of string comparison time. But this isn’t really about speed only. It’s about structure. Instead of dealing with raw text everywhere, we convert it into a small set of known building blocks. That makes the rest of the compiler much simpler to think about.

Enough talk. Let's get building.

### Defining tokens
We define an enum for *a few* of the kinds of tokens we need. Our first few lines of actual code.

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

What's with the negative numbers? In addition to the tokens above, we also have single character tokens like  `+`, `(`, and `)`. For these, we simply return their ASCII value. Saves us the headache of having a tok_* enum for every possible token. So single character tokens are positive ASCII values and multi-character and special-character tokens are negative enum values. No accidental collisions between single-character and multi-character tokens. We could also just use high positive numbers for the enum - beyond the ascii range. Both approaches are valid. All tokens are integers in the end. 

- `tok_def` → -3
- `+` → 43 (ASCII)
- `(` → 40 (ASCII)

We also use two global variables to carry extra data alongside a token return value:

```cpp
static string IdentifierStr; // Filled in if tok_identifier
static double NumVal;        // Filled in if tok_number
```

When the lexer sees the name `foo`, it returns `tok_identifier` and sets `IdentifierStr = "foo"`.
When it sees `3.14`, it returns `tok_number` and sets `NumVal = 3.14`.

## Reading Characters

Old Mac OS versions chose '\r' to represent a newline, the new Macs chose '\n', while Windows chose '\r\n'. Regardless of our personal beliefs, we can't be choosing sides if we want our programming language to be world-class. So we *normalize* the newlines which is to say we make them all the same - and we choose what this *same* is. Note that we don't modify the source in any way. We only change the internal representation so our compiler can have a more predictable newline token. We run with '\n'. The following is our helper function that reads one character at a time while *normalizing* newlines. 

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


## gettok(): Reading One Token at a Time

This is what we came here for. This is the function that reads the characters and converts them to tokens. It returns one token for every call.

```cpp
int gettok() {
  static int LastChar = ' ';
```

We initialize the static `LastChar` to a *space*. Then in the following loop we start reading over spaces until we reach some actual characters.

```cpp
  while (isspace(LastChar) && LastChar != '\n')
    LastChar = advance();
```

 Note that `gettok()` doesn't just skip spaces on the top of the file or the beginning of a line. It also skips spaces *between* tokens. Here's a concrete example:
 
 ```python
# main.pyxc

#  ^------- gettok() will skip all this space
def add(x,y):    
#  ^------- gettok() will skip this space too     
    return x + y
# ^------- gettok() will skip these spaces too
 ```
 
We could have initialized `LastChar` to something different like maybe an additional token **init_token** = *-1000000*  but then we'd have to write:
```cpp
  while ((isspace(LastChar) || LastChar == init_token) && LastChar != '\n')
    LastChar = advance();
```

By initializing it to a *space*, we avoid one additional test. If you think this is nitpicking, you're not wrong and it wouldn't be terrible to just use an init_token. This is not a hill we die on. 

Now let's look at how we read in different kinds of tokens. As a general rule, `LastChar` holds the last character read that *hasn't been consumed yet* by the end of gettok(). This lets the lexer figure out what kind of token it is looking at (number, identifier, etc). 

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
    
    // LastChar is a character that is neither an alphabet nor a number, i.e the last unprocessed character. 
  }
```

We accumulate letters, digits, and underscores into `IdentifierStr`, then check if it's a keyword. If it matches `"def"`, `"extern"`, or `"return"`, we return the appropriate keyword token. Otherwise it's a plain identifier.

Examples:
- `def` → `tok_def`
- `foo` → `tok_identifier`, `IdentifierStr = "foo"`
- `my_var` → `tok_identifier`, `IdentifierStr = "my_var"`

The if-chain is simple and clear for three keywords. In a later chapter, when we add more keywords (like `if`, `else`, `while`, `var`), we'll move this into a map. For now, we leave a TODO in the code so we remember to do it later.

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

There is a subtle issue though. `strtod` parses as far as it can and stops at the second `.`. The rest of `1.23.45.67` — the `.45.67` part — is effectively ignored. We'll fix this in a later chapter by checking where `strtod` stopped and erroring on leftover junk. For now, we leave a TODO in the code and move on — it's not a problem for valid input.

Next, we deal with newlines. We keep them instead of reading over them. In a Python-like language, newlines mark the end of statements — they're meaningful to the parser. So we preserve them. In C++, we could throw them away. 

### Newlines

```cpp
  if (LastChar == '\n') {
    LastChar = ' ';
    return tok_eol;
  }
```

We break the 'LastChar is the next unprocessed character' rule here. We don't call `advance()`. If we did call advance() here, the compiler would immediately go looking for the next character. If you're typing live into the terminal (the REPL), the program would just sit there staring at you, waiting for more input before it even finished processing the line you just hit Enter on. It’s the programming equivalent of an awkward silence. By setting LastChar = ' ' manually, we give the program a little nudge to keep moving. The next time gettok() is called, it will skip over it and read the actual next token.

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

In [Chapter 3](chapter-03.md), `TokenNames` grows to cover every possible token — including single-character ones like `+` and `(` — with friendlier names for error messages.

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

`tok_identifier` and `tok_number` are handled separately because the token value alone isn't enough — we need the associated data (`IdentifierStr` and `NumVal`) to be printed too. All other named tokens go through `TokenNames`. Single-character tokens (positive ASCII values) print as `'c'`.

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
