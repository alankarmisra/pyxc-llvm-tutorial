---
description: "Analyzing program words"
---
# 1. pyxc: Analyzing program words

## Starting small

I've been told writing compilers is hard. The more I think about it, the harder it seems. So I'm going to start small, and build from there. I think if I can make a little bit of progress each day, and not bet my joyful life on it, I can get there in one piece without shedding too many tears. If something doesn't make sense to me, I have the option of looking up references or implementations. I can also [discuss it](#need-help) with other people. I am not alone.

I've looked up the Kaleidoscope LLVM tutorial (let's call it `KT` for short). If you haven't, don't worry, I'll go through everything it covers here (and more). I think first I want to write out some pseudocode just to get an idea of the syntax I want and maybe a basic script. I've made plenty of calculators to learn languages so let's start with an `add` function.

```pyxc
# add.pyxc
# These are comments, like in python.
# Let's define our own function
def add(x, y):
    return x + y

printd(add(1, 2)) # call the add function and print it's value
```

I want this to print:

```bash
3
```

What's the `d` in `printd`? I don't want to go through the hassle of having type-checking etc. this early on in my programming language. So I think what I'll do is, allow only `double` both as input to and output of functions. And then `printd` can print a double so I know my function did the right thing. Later I can think about how to introduce other types. I think printd can baked into the language. I don't know how I'm going to do that, but one step at a time. 

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-01
```

## Understanding words

If I write this:

```pyxc
# an add function
def add(x,y):
    return x + y    
```


I personally know that `def` is a *keyword*, `add` is a *function name*, `x` and `y` are *parameters*, and so on. I also have a *comment* on the first line. So my first order of business is to relay this information to the compiler. I could just break my code up into word pieces: 'def', 'add', '(', 'x', ... but then the compiler will have to do string comparisons at each stage to check the grammar and emit the code and that is *no bueno*. I'll instead represent each item as a number instead I think so it's quicker for later stages to do comparisons. I'll define an *enum* of numbers so in my code, I can use enum values instead of confusing integers. I'm told this is called *lexing* (from Latin *lexis*, meaning word) and these individual enum values are called `tokens`. Ok, I'll call them that. But how do I represent function names, and variable names? I can't possibly guess what a user is going to name their function. So I think I'll just have a generic `identifier` token for names - regardless of whether they are function names or parameter names. I could have separated them, but I'll do that if and when I need to. So here's what I got so far:

```cpp
enum Token {
    tok_def,
    tok_identifier,
    tok_return
}
```

### Defining tokens

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

What's with the negative numbers? I've also got single-character tokens to deal with — `+`, `(`, `)`, and so on. I don't want a `tok_*` entry for every single one of those; there are dozens of them and it buys me nothing. So for anything that's just one character, I'll return its own ASCII value directly instead. That splits my token space cleanly in two: single characters live in the positive ASCII range, so I put my named tokens on the negative side. No collisions between the two, ever, and I don't need a case for every symbol I might add later. I could just as easily have picked large positive numbers past the ASCII range instead — either scheme works, negative is just the one I picked.

- `tok_def` → -3
- `+` → 43 (ASCII)
- `(` → 40 (ASCII)

Two of these tokens need to carry extra data along with them, though. `tok_identifier` and `tok_number` aren't much use on their own if the rest of the compiler can't tell *which* identifier or *which* number I just read. Rather than bolt that data onto the token itself, I'll keep two globals that `gettok()` fills in as a side effect whenever it returns one of these:

```cpp
static string IdentifierStr; // Filled in if tok_identifier
static double NumVal;        // Filled in if tok_number
```

When the lexer sees the name `foo`, it returns `tok_identifier` and sets `IdentifierStr = "foo"`.
When it sees `3.14`, it returns `tok_number` and sets `NumVal = 3.14`.

## Reading Characters

Before I write the actual token reader, I need a way to read characters I can trust. Old Mac OS used `\r` for a newline, the new Macs use `\n`, Windows uses `\r\n`. I don't want three different newline checks scattered through my lexer, so I'll normalize all of that down to `\n` at the one point where I actually read a character — everything downstream only ever has to think about `\n`. I'm not touching the source file itself here, just what my compiler sees internally.

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

This is what I actually came here to write — the function that reads characters and turns them into tokens, one token per call.

```cpp
int gettok() {
  static int LastChar = ' ';
```

I start the static `LastChar` off as a *space* rather than some sentinel value, because a space is something my whitespace-skipping loop already knows how to handle — no special first-call case needed. Then I loop over whitespace until I hit something real:

```cpp
  while (isspace(LastChar) && LastChar != '\n')
    LastChar = advance();
```

`gettok()` isn't just skipping space at the top of the file or the start of a line — since it runs fresh on every call, it skips space *between* tokens too. Here's a concrete example:
 
 ```pyxc
# main.pyxc

#  ^------- gettok() will skip all this space
def add(x,y):    
#  ^------- gettok() will skip this space too     
    return x + y
# ^------- gettok() will skip these spaces too
 ```
 
I could have initialized `LastChar` to something else instead — an **init_token** set to, say, *-1000000* — but then I'd have to check for it explicitly:
```cpp
  while ((isspace(LastChar) || LastChar == init_token) && LastChar != '\n')
    LastChar = advance();
```

Initializing to a *space* instead means I skip that extra check entirely. It's a small thing — not a hill I'd die on, and an init_token wouldn't be terrible either — but it's one less special case to carry around.

From here on, `LastChar` holds the last character I read that *hasn't been consumed yet* by the time `gettok()` returns. That's the rule every branch below leans on — it's how the lexer knows what kind of token it's looking at.

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

I accumulate letters, digits, and underscores into `IdentifierStr`, then check whether it matches one of my three keywords. If it does, I return that keyword's token. Otherwise it's just an identifier.

Examples:
- `def` → `tok_def`
- `foo` → `tok_identifier`, `IdentifierStr = "foo"`
- `my_var` → `tok_identifier`, `IdentifierStr = "my_var"`

An if-chain like this is fine for three keywords — I can read the whole thing at a glance. Once I add more keywords later (`if`, `else`, `while`, `var`), a chain of string comparisons stops being fine and I'll want a lookup table instead. I'm not there yet, so I'll leave a TODO in the code rather than build the table before I actually need it.

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

Digits and dots go through a similar accumulate-then-convert path: read everything that looks like it belongs to a number into `NumStr`, then hand the whole string to `strtod` and let it do the actual parsing into `NumVal`.

This works for the inputs I actually care about right now:
- `42` → `tok_number`, `NumVal = 42.0`
- `3.14` → `tok_number`, `NumVal = 3.14`
- `.5` → `tok_number`, `NumVal = 0.5`

There's a bug lurking here, though I'm going to let it lurk for now. `strtod` parses as far as it can and just stops at the second `.` — so `1.23.45.67` silently becomes `1.23`, and the `.45.67` part is dropped on the floor without a word. Fixing it properly means checking where `strtod` stopped and erroring on whatever's left over, which is more machinery than I want to build before I even have a parser to report the error to. TODO for later; it doesn't bite me on valid input.

Next: newlines. Unlike other whitespace, I don't want to throw these away. pyxc is Python-like, and in a Python-like language a newline marks the end of a statement — it's meaningful to the parser. So instead of skipping over it like I do with spaces, I hand it forward as a real token.

### Newlines

```cpp
  if (LastChar == '\n') {
    LastChar = ' ';
    return tok_eol;
  }
```

This is the one place where I break my own "`LastChar` is always the next unconsumed character" rule — I don't call `advance()` here. If I did, `gettok()` would immediately go looking for whatever comes after the newline before it even returns. Reading from a file that's harmless, but in the REPL it's fatal: the program would just sit there waiting for another keystroke before it's told you anything about the line you already typed and hit Enter on. So instead I reset `LastChar` to a space and return right away. Next call, the whitespace loop eats that space like it eats any other, and moves on to the real next token.

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

Comments run from `#` to the end of the line, same as Python. I don't keep any of it — read forward to the newline (or EOF) and throw the whole thing away, then return `tok_eol` as if the comment line had been blank all along.

Same `LastChar = ' '` trick as the newline case, and for the same reason — return immediately instead of blocking the REPL.

If the comment runs straight into `EOF` with no trailing newline, `LastChar` ends up `EOF` and I fall through to the EOF case below.

### End Of File
```cpp
  if (LastChar == EOF)
    return tok_eof;
```

Nothing left to check but end of file. I return it without consuming it, so nothing downstream has to guess whether there's more input coming.

### Everything Else

```cpp
  int ThisChar = LastChar;
  LastChar = advance();
  return ThisChar;
}
```

If none of the above matched, whatever's left is a single character on its own — `+`, `(`, `:`, and so on. I return its ASCII value straight through; the parser will compare against character literals like `'+'` or `'('` directly, no separate token needed.

## The Driver

I need two more things before I can actually watch this work: something that turns a token number into a name I can read, and a `main()` that drives the loop and prints what it sees.

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

In [Chapter 3](chapter-03.md), `TokenNames` grows to cover every possible token — including single-character ones like `+` and `(` — with friendlier names for error messages. I don't need that yet, just enough to see `gettok()` working.

`main()` calls `gettok()` in a loop and prints each token, until it hits EOF:

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

`tok_identifier` and `tok_number` need special-casing here because the token value alone doesn't tell the whole story — I also want to see the actual identifier text or number, which is exactly why I stashed them in those two globals earlier. Everything else with a name goes through `TokenNames`. Single-character tokens (positive ASCII values) print as `'c'`.

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

In [Chapter 2](chapter-02.md) I build the parser on top of the lexer. The parser reads the token stream and works out the structure — that `def add(x, y)` is a function taking two arguments, that `x + y` is an addition.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version` and `ninja --version`

We'll figure it out.
