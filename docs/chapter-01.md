---
description: "Analyzing program words"
---
# 1. pyxc: Analyzing program words

## Starting small

I've been told writing compilers is hard. The more I think about it, the harder it seems. So I'm going to start small, and build from there. 

For starters, I'll write out some small programs just to get an idea of the syntax I want. I've made plenty of calculators to learn new programming languages so let's start with an `add` function.

```pyxc
# add.pyxc
# These are comments, like in python.
# Let's define our own function
def add(x, y):
    return x + y

print(add(1, 2)) # call the add function and print its value
```

I want this to print:

```bash
3
```

pyxc will eventually have data types because I want it to compile down to binary with runtime efficiencies approaching C. But for now I'm skipping types and assuming *double* for both, function input and output, so in the code above, `x`, `y` and the return value of `add` will implicitly be *double*. 

I think this is a good enough scope for some initial experimentation.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-01
```

## Understanding words

When I write this:

```pyxc
# an add function
def add(x,y):
    return x + y    
```

*I* know that `def` defines a function, `add` is the *function name*, `x` and `y` are *parameters*, and *comments* follow the *#* character. I have to represent this structure somehow in a way that allows me to analyze the syntax and grammar. 

For analysis, I could just pass around the strings 'def', 'add', '(', 'x', ... but then I have to do string comparisons at each analysis stage. Comparing ['d', 'e', 'f'] with ['d', 'e', 'f'] is 3 character comparisons. This will add up over multiple strings being compared during analysis. Instead I'll use an enum to represent the different words I see. This way future comparisons are just one integer comparison. 

```cpp
enum Token {
    tok_def, // the keyword 'def'
    tok_return // the keyword 'return'
}
```

!!!note
    Breaking up the source into words is called *lexing* (from Latin *lexis*, meaning word) and these individual enum values, we will call `tokens` because that's what people who wrote compilers before us called them.  

How do I represent function and variable names? I can create a catch-all `tok_identifier` to represent all kinds of names. 

I'll do the same for numbers with `tok_number`. 

But with both of these, I'll need to know *which name* and *which number* I saw in the source. Since I'm analyzing things one token at a time, I'll create a global variable for each:

```cpp
static string IdentifierStr; // Filled in if tok_identifier
static double NumVal;        // Filled in if tok_number
```

When I read the name `foo`, I return `tok_identifier` and set `IdentifierStr = "foo"`.
When I read `3.14`, I return `tok_number` and set `NumVal = 3.14`.

Since pyxc is python like, I also need to consider new lines. So let's have a `tok_eol`. And finally, we need to indicate that we've reached the end of file somehow, so let's also do a `tok_eof`. 

So here's what I got so far (and the negative numbers might surprise you, but I'll explain!):

```cpp
enum Token {
  tok_eof = -1,  // End of file
  tok_eol = -2,  // End of line ('\n')

  tok_def    = -3,  // 'def' keyword

  tok_identifier = -4, // Variable/function names: foo, my_var
  tok_number     = -5, // Numbers: 42, 3.14

  tok_return = -6 // 'return' keyword
};
```

Notice I haven't dealt with single-character tokens like `+`, `(`, `)`, and so on — they only take one comparison anyway, so defining an enum entry for each would be busywork (it also simplifies error reporting later — more on that when I get there). For anything that's just one character, I return its own ASCII value directly instead, which is always positive. So I made my named tokens negative — a trick borrowed from the Kaleidoscope tutorial — keeping the two ranges from ever colliding. Large positive numbers past the ASCII range would have worked just as well; negative was simply Kaleidoscope's choice, and I kept it.

- `tok_def` → -3
- `+` → 43 (ASCII)
- `(` → 40 (ASCII)

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

`gettok()` is where I read characters and turn them into tokens, one token per call.

```cpp
int gettok() {
  static int LastChar = ' ';
```

I start the static `LastChar` off as a *space* rather than some sentinel value, because a space is something my whitespace-skipping loop already knows how to handle — no special first-call case needed. Then I loop over whitespace until I hit something real:

```cpp
  while (isspace(LastChar) && LastChar != '\n')
    LastChar = advance();
```

I'm not just skipping space at the top of the file or the start of a line — since `gettok()` runs fresh on every call, I skip space *between* tokens too. Here's a concrete example:
 
 ```pyxc
# main.pyxc

#  ^------- I'll skip all this space
def add(x,y):    
#  ^------- I'll skip this space too     
    return x + y
# ^------- I'll skip these spaces too
 ```
 
I could have initialized `LastChar` to something else instead — an **init_token** set to, say, *-1000000* — but then I'd have to check for it explicitly:
```cpp
  while ((isspace(LastChar) || LastChar == init_token) && LastChar != '\n')
    LastChar = advance();
```

Initializing to a *space* instead means I skip that extra check entirely. It's a small thing — not a hill I'd die on, and an init_token wouldn't be terrible either — but it's one less special case to carry around.

From here on, `LastChar` holds the last character I read that *hasn't been consumed yet* by the time `gettok()` returns. That's the rule every branch below leans on — it's how I can tell what kind of token I'm looking at.

### Identifiers and Keywords

```cpp
  if (isalpha(LastChar) || LastChar == '_') {
    IdentifierStr = LastChar;
    while (isalnum(LastChar = advance()) || LastChar == '_')
      IdentifierStr += LastChar;
    
    if (IdentifierStr == "def")    return tok_def;
    if (IdentifierStr == "return") return tok_return;
    
    return tok_identifier;
    
    // LastChar is a character that is neither an alphabet nor a number, i.e the last unprocessed character. 
  }
```

I accumulate letters, digits, and underscores into `IdentifierStr`, then check whether it matches one of my two keywords. If it does, I return that keyword's token. Otherwise it's just an identifier.

Examples:
- `def` → `tok_def`
- `foo` → `tok_identifier`, `IdentifierStr = "foo"`
- `my_var` → `tok_identifier`, `IdentifierStr = "my_var"`

An if-chain like this is fine for two keywords — I can read the whole thing at a glance. Once I add more keywords later (`extern`, `if`, `else`, `while`, `var`), a chain of string comparisons stops being fine and I'll want a lookup table instead. I'm not there yet, so I'll leave a TODO in the code rather than build the table before I actually need it.

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

I take numbers through a similar accumulate-then-convert path: I read everything that looks like it belongs to a number into `NumStr`, then hand the whole string to `strtod` to parse into `NumVal`.

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

This is the one place where I break my own "`LastChar` is always the next unconsumed character" rule — I don't call `advance()` here. If I did, I'd immediately go looking for whatever comes after the newline before I even return from `gettok()`. Reading from a file that's harmless, but in the REPL it's fatal: I'd just sit there waiting for another keystroke before telling you anything about the line you already typed and hit Enter on. So instead I reset `LastChar` to a space and return right away. On the next call, I skip that space the same way I skip any other whitespace, then move on to the real next token.

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

If none of the above matched, whatever's left is a single character on its own — `+`, `(`, `:`, and so on. I return its ASCII value straight through; in the next chapter I'll compare it against character literals like `'+'` or `'('` directly in the parser, no separate token needed.

## The Driver

I need two more things before I can actually watch this work: something that turns a token number into a name I can read, and a `main()` where I drive the loop and print what I see.

`TokenNames` maps each named token to a readable string:

```cpp
static map<int, string> TokenNames = {
    {tok_eof,        "tok_eof"},
    {tok_eol,        "tok_eol"},
    {tok_def,        "tok_def"},
    {tok_identifier, "tok_identifier"},
    {tok_number,     "tok_number"},
    {tok_return,     "tok_return"},
};
```

In [Chapter 3](chapter-03.md), I grow `TokenNames` to cover every possible token — including single-character ones like `+` and `(` — with friendlier names for error messages. I don't need that yet, just enough to see `gettok()` working.

In `main()`, I call `gettok()` in a loop and print each token, until I hit EOF:

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

In [Chapter 2](chapter-02.md) I build a parser on top of the lexer. I read the token stream and work out the structure — that `def add(x, y)` is a function taking two arguments, that `x + y` is an addition.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version` and `ninja --version`

We'll figure it out.
