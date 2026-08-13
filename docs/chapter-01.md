---
section: "Foundations"
description: "Analyzing program words"
---
# 1. pyxc: Analyzing program words

## Starting small

I've been told writing compilers is hard. The more I think about it, the harder it seems. So I'm going to stop thinking about it too much, start small, and build from there.

I'll begin by writing a few small programs to get an idea of the syntax I want. I've built plenty of calculators while learning new programming languages, so I'll start with an `add` function:

```pyxc
# add.pyxc
def add(x, y): # define a function
    x + y # return the sum

print(add(1, 2)) # call the add function and print its value
```

I want this to print:

```text
3
```

I will eventually add data types to pyxc. For now, I'll assume that every function parameter and return value is a `double`. In this example, `x`, `y`, and the value returned by `add` are all implicitly `double`.

I'll also restrict each function body to a single **expression**, i.e., something that evaluates to, or *expresses*, a value. The value of that expression becomes the result of the function, so I don't need a `return` keyword just yet. Once I support multiple statements inside a function, I'll introduce `return` so I can say explicitly which value the function returns.

For now, I think this is enough to begin experimenting.

## Source Code

```text
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-01
```

## Understanding Words

When I write this:

```pyxc
# an add function
def add(x, y):
    x + y
```

I can already recognize some meaning in the characters. `def` defines a function, `add` is the *function name*, `x` and `y` are *parameters*, and a *comment* follows the `#` character.

pyxc initially sees only a flat stream of characters. I need to separate those characters into meaningful pieces before I can organize them into a structured, perhaps hierarchical form. I can then check that structure for syntax errors and work out what the program means.

I could pass strings such as `"def"`, `"add"`, `"("`, and `"x"` through every stage of the compiler. But then every stage would need to know which spellings have special meaning and repeatedly compare strings to recognize them. Instead, I will recognize each fixed piece of syntax once and represent its kind with an enum:

```cpp
enum Token {
    tok_def, // the keyword 'def'
};
```

Later, I can ask whether something is `tok_def` with one integer comparison instead of comparing its characters with `def` again.

!!!note
    Breaking up the source into words is called *lexing*. The word comes from the Greek *lexis*, meaning “word” or “speech.”

I call each resulting piece a *token* which represents something in the original source. Here, `tok_def` represents the characters `def`.

How do I represent function and variable names when each programmer can invent new ones? I cannot create an enum value for every possible name. Instead, I create a catch-all `tok_name` to signal that I have read a name, then keep the name itself in a separate variable.

### Reading names

I need somewhere to store the actual name that `tok_name` represents:

```cpp
static string Name; // I store the name I just read.
```

When I read the name `foo`, I return `tok_name` and set `Name` to `"foo"`.

One variable is enough because I always use `Name` before calling `getToken()` again. The next call may overwrite it with another name. In this chapter, I only print `Name` immediately. I will start copying it into longer-lived structures in the next chapter.

### Reading numbers

I'll use the same approach for numbers. `tok_number` tells me that I read a number, while `NumberValue` stores its value:

```cpp
static double NumberValue; // I store the number I just read.
```

When I read `3.14`, I return `tok_number` and set `NumberValue` to `3.14`.

### Newlines matter

pyxc doesn't use `;` to end a statement. A newline does that job instead. So I need to recognize newlines as tokens. I'll add a `tok_eol`.

### The end-of-file (EOF) marker

Finally, I need to indicate that I've reached the end of input. I'll add `tok_eof` to represent the end-of-file marker.

### Punctuation and all the rest

Adding the punctuation from the sample to the token kinds I've identified so far, I end up with:

```cpp
//===----------------------------------------===//
// Lexer
//===----------------------------------------===//

// I return one of these named tokens from the lexer. I report characters that
// do not belong to the language as tok_error.
enum Token {
  // input boundaries
  tok_eof = 1,
  tok_eol,

  // lexer errors
  tok_error,

  // function definitions
  tok_def,

  // tokens that need additional information attached
  tok_name,
  tok_number,

  // punctuation and operators
  tok_lparen,
  tok_rparen,
  tok_comma,
  tok_colon,
  tok_plus,
};
```

## Reading Newlines

Old Mac OS used `\r` for a newline, modern macOS and Unix use `\n`, and Windows uses `\r\n`. I don't want the rest of the lexer to handle three forms of newline, so I'll normalize all of them to `\n` as I read them.

I'm not changing the source file itself. I'm only changing the characters that my code sees internally.

```cpp
/// advance - I return the next character, normalizing `\r\n` (Windows)
/// and bare `\r` (Old Macs) into `\n`.
int advance() {
  int LastChar = getchar();

  // case: '\r' or '\r\n'
  if (LastChar == '\r') {
    int NextChar = getchar();

    // A following '\n' is part of the same line ending; eat it.
    // Anything else belongs to the next token; put it back.
    // (EOF can't be put back at all, so it's excluded from that check.
    // The next getchar() will still return EOF, so we don't lose it.)
    if (NextChar != '\n' && NextChar != EOF) {
      ungetc(NextChar, stdin);
    }
    return '\n';
  }

  // case '\n' or any other non-newline character
  return LastChar;
}
```

## Generating One Token At A Time

`getToken()` is where I read characters and turn them into tokens, one per call.

```cpp
/// getToken - I return the next token from standard input.
int getToken() {
  static int LastChar = ' ';

  // I skip whitespace except newlines.
  while (isspace(LastChar) && LastChar != '\n')
    LastChar = advance();
```

I initialize the static `LastChar` to a *space* so the loop runs the first time I call `getToken()`. This space is only a starting value; it did not come from the source file.

- On the first call, `advance()` reads the first input character, and the loop continues past any whitespace at the start of the file.
- On later calls, the loop skips whitespace between tokens the same way.

I stop the loop at a newline, a non-whitespace character, or end of file:

After this loop, `LastChar` holds the next input value to process which could be one of these: 
- the first character of the next token
- a newline, or 
- `EOF`.

### Names and Keywords

I recognize a name when it begins with a letter or underscore. I then accumulate letters, digits, and underscores into `Name` and check whether it matches one of my keywords (only one for now). If it does, I return that keyword's token. Otherwise, I return `tok_name`.

I'll put all my keywords in a map so I can look up a name and get its token.

```cpp
static map<string, Token> Keywords = {
    {"def", tok_def},
};
```

I can now collect the characters in a name and return either its keyword token or `tok_name`.

```cpp
  // I read a name or keyword.
  if (isalpha(LastChar) || LastChar == '_') {
    Name = LastChar;
    while (isalnum(LastChar = advance()) || LastChar == '_')
      Name += LastChar;
    // I leave the first character that is not part of this name or keyword in
    // LastChar.

    // I check whether the name is a keyword.
    auto KeywordIt = Keywords.find(Name);
    if (KeywordIt != Keywords.end())
      return KeywordIt->second;
    return tok_name;
  }
```

`LastChar` now acts as one-character lookahead. It holds the first character that is not part of the name, which could be whitespace, punctuation, or `EOF`. Because `LastChar` is static, the next call to `getToken()` starts with that same character instead of reading past it.

Examples:

- `def` → `tok_def`
- `foo` → `tok_name`, `Name = "foo"`
- `my_var` → `tok_name`, `Name = "my_var"`

### Numbers

I handle numbers similarly. I collect everything that looks like part of a number in `NumStr`, then give the complete string to [strtod](https://en.cppreference.com/w/cpp/string/byte/strtof), which converts it to a double. I put this double into `NumberValue`.

```cpp
  // I read a number.
  if (isdigit(LastChar) || LastChar == '.') {
    string NumStr;
    do {
      NumStr += LastChar;
      LastChar = advance();
    } while (isdigit(LastChar) || LastChar == '.');
    // I leave the first character that is not part of this number in LastChar.

    // TODO: I consume all of 1.23.45.67 but parse it as 1.23.
    NumberValue = strtod(NumStr.c_str(), 0);
    return tok_number;
  }
```

This works for the inputs I actually care about right now:

- `42` → `tok_number`, `NumberValue = 42.0`
- `3.14` → `tok_number`, `NumberValue = 3.14`
- `.5` → `tok_number`, `NumberValue = 0.5`

There is a bug here, but I'll leave it for now. I collect an invalid number such as `1.23.45.67` into a single `NumStr`. `strtod` converts the valid `1.23` prefix and ignores the rest. Because I have already consumed `.45.67` from the input stream, those characters disappear instead of producing an error. A lone `.` is also accepted and becomes `0.0`. I'll leave a *TODO* to fix this later. For now, valid numbers work, and I want to keep this proof of concept small.

### Comments

Comments run from `#` to the end of the line. I don't keep the comment text. I read forward until I reach a newline or `EOF`. If I find a newline, I consume it and return `tok_eol`. If I find `EOF`, I leave it for the `EOF` case below.

```cpp
  // I discard a comment.
  if (LastChar == '#') {
    // I consume characters through the end of the line.
    do {
      LastChar = advance();
    } while (LastChar != '\n' && LastChar != EOF);

    if (LastChar != EOF) {
      LastChar = advance();
      return tok_eol;
    }
  }
```

### Newlines

When `LastChar` holds a normalized newline, I call `advance()` so `LastChar` holds the next character, then return `tok_eol`.

```cpp
  // I recognize a newline.
  if (LastChar == '\n') {
    LastChar = advance();
    return tok_eol;
  }
```

### End Of File

When `LastChar` equals `EOF`, the input stream has no more data, so I return `tok_eof`.

```cpp
  // I recognize the end of the file.
  if (LastChar == EOF)
    return tok_eof;
```

### Punctuation and Operators

Finally, I handle the single-character punctuation and operators. I save the current character in `ThisChar`, call `advance()` so `LastChar` points to the next character, then use a `switch` on `ThisChar`. If it is not one of the characters I recognize, I return `tok_error`.

```cpp
  // I read single-character punctuation and operators.
  int ThisChar = LastChar;
  LastChar = advance();
  switch (ThisChar) {
  case '(':
    return tok_lparen;
  case ')':
    return tok_rparen;
  case ',':
    return tok_comma;
  case ':':
    return tok_colon;
  case '+':
    return tok_plus;
  default:
    return tok_error;
  }
}
```

## Testing Token Generation

I'll feed pyxc source into my lexer and print the tokens it generates. To make the token values readable, I map each one to a display string. I'll keep using the enum values everywhere else in the compiler. These strings are only for debug output and, later, for error reporting.

```cpp
// I map each named token to a readable string for debug output and error
// reporting.
static map<int, string> TokenNames = {
    {tok_eof, "end of input"},
    {tok_eol, "newline"},
    {tok_error, "error"},
    {tok_def, "'def'"},
    {tok_name, "name"},
    {tok_number, "number"},
    {tok_lparen, "'('"},
    {tok_rparen, "')'"},
    {tok_comma, "','"},
    {tok_colon, "':'"},
    {tok_plus, "'+'"},
};
```

I now have what I need. In `main()`, I call `getToken()` in a loop and print each token until I reach `tok_eof`:

```cpp
//===----------------------------------------===//
// Driver
//===----------------------------------------===//

int main() {
  int Token;
  while ((Token = getToken()) != tok_eof) {
    if (Token == tok_name)
      fprintf(stdout, "%s: %s\n", TokenNames.at(Token).c_str(),
              Name.c_str());
    else if (Token == tok_number)
      fprintf(stdout, "%s: %g\n", TokenNames.at(Token).c_str(), NumberValue);
    else
      fprintf(stdout, "%s\n", TokenNames.at(Token).c_str());
  }
  return 0;
}
```

For `tok_name` and `tok_number`, the token alone is not enough, so I also print the value stored in `Name` or `NumberValue`.

## Build and Run

```bash
cd code/chapter-01
cmake -S . -B build && cmake --build build
printf "# add.pyxc\ndef add(x, y): # define a function\n    x + y # return the sum\n\nprint(add(1, 2)) # call the add function and print its value\n" | ./build/pyxc
```

```text
newline
'def'
name: add
'('
name: x
','
name: y
')'
':'
newline
name: x
'+'
name: y
newline
newline
name: print
'('
name: add
'('
number: 1
','
number: 2
')'
')'
newline
```

Comments do not produce tokens of their own, but the newline at the end of a comment-only line still produces `tok_eol`. Blank lines produce `tok_eol` for the same reason. I'm okay with this behavior.

## `lit` or `llvm-lit` Quickstart

I use LLVM's `lit` test runner for all my tests. Let me walk through one test.

`test/sample_chapter_1.pyxc` contains the same `add.pyxc` program from earlier in this chapter. I add `RUN` and `CHECK` instructions at the top:

```pyxc
# RUN: %pyxc < %s | FileCheck %s --match-full-lines

# CHECK: 'def'
# CHECK: name: print
# CHECK: number: 1
# CHECK: number: 2

# sample: the add.pyxc example from the doc, run end-to-end through the lexer.
# add.pyxc
def add(x, y): # define a function
    x + y # return the sum

print(add(1, 2)) # call the add function and print its value
```

These instructions begin with `#`, so pyxc ignores their text as comments. Their line endings still produce newline tokens. `lit` and `FileCheck`, however, know how to read the comment instructions.

I start with the `RUN` instruction:

```pyxc
# RUN: %pyxc < %s | FileCheck %s --match-full-lines
```

It tells `lit` which command to run for this test.

Before running the command, `lit` replaces two placeholders:

- `%pyxc` becomes the path to the pyxc executable, such as `build/pyxc`. I define that substitution in `test/lit.cfg.py`:

  ```python
  config.substitutions.append(
      ("%pyxc", os.path.join(chapter_dir, "build", "pyxc"))
  )
  ```

- `%s` becomes the path to the current test file, such as `test/sample_chapter_1.pyxc`.

After those replacements, the command is roughly:

```bash
build/pyxc < test/sample_chapter_1.pyxc |
    FileCheck test/sample_chapter_1.pyxc --match-full-lines
```

The command uses the test file in two different ways.

First, `< test/sample_chapter_1.pyxc` sends the entire file to pyxc as input. Pyxc ignores the comment lines and tokenizes the sample program.

Second, the pipe sends pyxc's output to `FileCheck`. The filename after `FileCheck` tells it where to find the `CHECK` instructions. In this case, those instructions are in the same test file:

```pyxc
# CHECK: 'def'
# CHECK: name: print
# CHECK: number: 1
# CHECK: number: 2
```

`FileCheck` looks for these lines in the lexer output in the order I wrote them:

1. It looks for a complete line matching `'def'`.
2. From that point onward, it looks for a complete line matching `name: print`.
3. It then looks for a complete line matching `number: 1`.
4. Finally, it looks for a complete line matching `number: 2`.

The lines do not have to be next to one another. The lexer can print other tokens between them. They only need to appear in the specified order.

I use `--match-full-lines` because I want each `CHECK` pattern to match a whole output line. For example, `number: 1` should match this:

```text
number: 1
```

It should not pass merely because those characters appear inside a longer line.

Each check corresponds to part of the sample program:

- `'def'` is the line printed for the `def` keyword. `TokenNames` maps `tok_def` to the string `'def'`.
- `name: print` is the line printed for the `print` name.
- `number: 1` and `number: 2` are printed for the numeric literals in `add(1, 2)`.

If `FileCheck` finds all four lines in order, it exits with `0`, and the `RUN` instruction passes. If a line is missing or appears in the wrong order, `FileCheck` exits with a nonzero value. `lit` then reports the test as failed and points me to the `CHECK` instruction it could not satisfy.

Running this on my sample test file:

```bash
llvm-lit test/sample_chapter_1.pyxc -v
```

I get:

```text
-- Testing: 1 tests, 1 workers --
PASS: pyxc-chapter01 :: sample_chapter_1.pyxc (1 of 1)

Testing Time: 0.12s

Total Discovered Tests: 1
  Passed: 1 (100.00%)
```

The `test/` directory has lit tests covering the lexer behavior I have implemented so far. I run the suite with:

```bash
llvm-lit test/
```

```text
-- Testing: 13 tests, 8 workers --
PASS: pyxc-chapter01 :: number_integer.pyxc (1 of 13)
PASS: pyxc-chapter01 :: comment_discards.pyxc (2 of 13)
PASS: pyxc-chapter01 :: comment_eof.pyxc (3 of 13)
PASS: pyxc-chapter01 :: name_simple.pyxc (4 of 13)
PASS: pyxc-chapter01 :: error_unknown_character.pyxc (5 of 13)
PASS: pyxc-chapter01 :: number_leading_dot.pyxc (6 of 13)
PASS: pyxc-chapter01 :: sample_chapter_1.pyxc (7 of 13)
PASS: pyxc-chapter01 :: name_underscore.pyxc (8 of 13)
PASS: pyxc-chapter01 :: keyword_def.pyxc (9 of 13)
PASS: pyxc-chapter01 :: punctuation_tokens.pyxc (10 of 13)
PASS: pyxc-chapter01 :: number_decimal.pyxc (11 of 13)
PASS: pyxc-chapter01 :: newline_normalization.pyxc (12 of 13)
PASS: pyxc-chapter01 :: token_boundaries.pyxc (13 of 13)

Testing Time: 0.68s

Total Discovered Tests: 13
  Passed: 13 (100.00%)
```

The test order and timing above are from one run; `lit` runs tests in parallel across several workers, so both vary from run to run.

## Try It

Earlier I mentioned that the lexer consumes all of `1.23.45.67` but reports only the valid `1.23` prefix. Here it is for real:

```bash
printf '1.23.45.67\n' | ./build/pyxc
```

```text
number: 1.23
newline
```

`strtod` stops at the second `.`, and I've already consumed the rest of the characters from the input stream reading `NumStr`, so `.45.67` never gets a chance to become its own token or trigger an error. It just disappears.

## What's Next

[Chapter 2](chapter-02.md) turns tokens into a syntax tree with a recursive-descent parser.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:

- Your OS and version
- Full error message
- Output of `cmake --version`

I'll help you figure it out.
