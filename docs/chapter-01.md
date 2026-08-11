---
section: "Foundations"
description: "Analyzing program words"
---
# 1. pyxc: Analyzing program words

## Starting small

I've been told writing compilers is hard. The more I think about it, the harder it seems. So I'm going to start small, and build from there. 

For starters, I'll write out some small programs just to get an idea of the syntax I want. I've made plenty of calculators to learn new programming languages so I'll start with an `add` function.

```pyxc
# add.pyxc
def add(x, y): # define a function
    x + y # return the sum

print(add(1, 2)) # call the add function and print its value
```

I want this to print:

```bash
3
```

I will eventually add data types to pyxc because I want to compile it down to binary with runtime efficiencies approaching C. For now, I'm skipping types and assuming *double* for both function input and output, so in the code above, `x`, `y`, and the return value of `add` are implicitly *double*.

I'm also restricting a function to just a single **expression**, i.e. anything that *expresses*, computes down to, a value which is returned from the function. I will eventually support multi-statement functions, conditionals like `if`, `elif`, `else` and will introduce `return` statements to know what to return and when. 

For now, I think this is a good enough scope for some initial experimentation.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-01
```

## Understanding Words

When I write this:

```pyxc
# an add function
def add(x,y):
    x + y    
```

*I* know that `def` defines a function, `add` is the *function name*, `x` and `y` are *parameters*, and *comments* follow the *#* character. I need a way to represent this structure so I can check the program for correctness. 

For analysis, I could just pass around the strings "def", "add", "(", "x", ... but then I have to do string comparisons at each analysis stage. Comparing ['d', 'e', 'f'] with ['d', 'e', 'f'] is 3 character comparisons. This will add up over multiple strings being compared during analysis. Instead I'll use an enum to represent the different words I see. This way future comparisons are just one integer comparison. 

```cpp
enum Token {
    tok_def, // the keyword 'def'
};
```

!!!note
    Breaking up the source into words is called *lexing* (from Latin *lexis*, meaning word) and these individual enum values, I will call `tokens`, because that's what people who wrote compilers before me called them.  

How do I represent function and variable names when each programmer can invent new ones? I cannot have an enum for every possible name. Instead, I create a catch-all `tok_name` to signal that I read a name and keep the name itself in a separate variable.

```cpp
static string Name; // I store the name I just read.
```

When I read the name `foo`, I return `tok_name` and set `Name = "foo"`.

One variable is enough because I always read `Name` and copy it out before asking the lexer for another token, the only thing that would overwrite it. 

I'll do the same for numbers with `tok_number`. 

```cpp
static double NumberValue; // I store the number I just read.
```

When I read `3.14`, I return `tok_number` and set `NumberValue = 3.14`.

Since pyxc is Python-like, I also need to consider new lines. So I'll add a `tok_eol` instead of ignoring newlines like I do with spaces. 

And finally, I need to indicate that I've reached the end of file somehow, so I'll also add a `tok_eof`. 

Adding the punctuation and remaining keywords from the sample I end up with:

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

I have one token, `tok_eol`, for a newline. However, Old Mac OS used `\r` for a newline, modern macOS and Unix use `\n`, and Windows uses `\r\n`. I don't want three different newline checks in my lexer, so I'll normalize all of them to `\n`. Then I only need to handle `\n`, which I convert to `tok_eol` for the rest of the compiler. I'm not touching the source file itself here, only what my code sees internally. I use `advance()` to read one character and normalize any newline to `\n`.

```cpp
/// advance - I return the next character, coalescing `\r\n` (Windows) into
/// `\n` and converting bare `\r` (Old Macs) into `\n`.
int advance() {
  int LastChar = getchar();

  if (LastChar == '\r') {
    int NextChar = getchar();
    if (NextChar != '\n' && NextChar != EOF)
      ungetc(NextChar, stdin);
    return '\n';
  }

  return LastChar;
}
```


## Generating One Token At A Time

`getToken()` is where I read characters and turn them into tokens, one per call. 

```cpp
/// getToken - I return the next token from standard input.
int getToken() {
  static int LastChar = ' ';
```

I start the static `LastChar` off as a *space*. Right after this, I skip whitespace other than newlines in a loop. On the first call, I skip past the initialization space and any whitespace at the start of the file. On later calls, I skip whitespace between tokens the same way. I stop this loop at a newline, a non-whitespace character, or end of file:

```cpp
  // I skip whitespace except newlines.
  while (isspace(LastChar) && LastChar != '\n')
    LastChar = advance();
```

After this loop, `LastChar` holds the next input value to process: the first character of the next token, a newline, or `EOF`.

### Names and Keywords

I recognize a name when it begins with a letter or underscore. I then accumulate letters, digits, and underscores into `Name` and check whether it matches one of my keywords (only one for now). If it does, I return that keyword's token. Otherwise, I return `tok_name`.

I'll dump all my keywords into a map for easy lookup and conversion.

```cpp
static map<string, Token> Keywords = {
    {"def", tok_def},
};
```

And now I collect characters and return an appropriate token.

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

`LastChar` holds the first character that is not part of the name or keyword. I'll keep this in mind; it'll help me track where I am in the input stream later on. It could be any character that is not valid in a name, including whitespace and punctuation, or `EOF`. 

Examples:

- `def` → `tok_def`
- `foo` → `tok_name`, `Name = "foo"`
- `my_var` → `tok_name`, `Name = "my_var"`

### Numbers

I take numbers through a similar accumulate-then-convert path: I read everything that looks like it belongs to a number into `NumStr`, then hand the whole string to [strtod](https://en.cppreference.com/w/cpp/string/byte/strtof) to parse into `NumberValue`.

```cpp  
  // I read a number.
  if (isdigit(LastChar) || LastChar == '.') {
    string NumStr;
    do {
      NumStr += LastChar;
      LastChar = advance();
    } while (isdigit(LastChar) || LastChar == '.');
    // I leave the first character that is not part of this number in LastChar.

    // TODO: I incorrectly lex 1.23.45.67 as 1.23.
    NumberValue = strtod(NumStr.c_str(), 0);
    return tok_number;
  }
```

This works for the inputs I actually care about right now:

- `42` → `tok_number`, `NumberValue = 42.0`
- `3.14` → `tok_number`, `NumberValue = 3.14`
- `.5` → `tok_number`, `NumberValue = 0.5`

There is a bug here, but I'll leave it for now. I collect an invalid number like `1.23.45.67` into a single `NumStr`. `strtod` accepts and converts only the `1.23` prefix and ignores the rest, and I've already consumed `.45.67` from the input stream, so it disappears instead of producing an error. Similarly `.` gets parsed as a `0`. I'll leave a *TODO* to fix this later; for now, valid numbers work and I want to keep this proof of concept small.

### Newlines

When `LastChar` holds a normalized newline, I return `tok_eol`. I read in another character to keep my promise that `LastChar` always holds the next character I haven't consumed yet. 

```cpp
  // I recognize a newline.
  if (LastChar == '\n') {
    LastChar = advance();
    return tok_eol;
  }
```

### Comments

Comments run from `#` to the end of the line, same as Python. I don't keep any of it. I read forward to the newline (or `EOF`), throw the whole thing away, then return `tok_eol`. If a comment follows code on a line, I use that trailing `tok_eol` to mark the code line as complete.

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

If the comment runs straight into `EOF` with no trailing newline, `LastChar` holds `EOF` and I fall through to the EOF case below.

### End Of File

When `LastChar` holds an `EOF`, the input stream has no more data, and I return `tok_eof`.

```cpp
  // I recognize the end of the file.
  if (LastChar == EOF)
    return tok_eof;
```


### Punctuation and Operators

And finally, I handle punctuation, operators, and unrecognized characters. Since these are single-character comparisons, I handle them with a simple `switch`. 

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

I’ll pipe pyxc source into my lexer and print the tokens I generate. To make those token values readable, I map each token to a display string. I'll keep using the enum values themselves everywhere else in the compiler; these strings are only for debug output and, later, for error reporting.

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

OK, I have what I need now, so in `main()`, I call `getToken()` in a loop and print each token, until I hit `tok_eof`:

```cpp
//===----------------------------------------===//
// Driver
//===----------------------------------------===//

int main() {
  int tok;
  while ((tok = getToken()) != tok_eof) {
    if (tok == tok_name)
      fprintf(stdout, "%s: %s\n", TokenNames.at(tok).c_str(),
              Name.c_str());
    else if (tok == tok_number)
      fprintf(stdout, "%s: %g\n", TokenNames.at(tok).c_str(), NumberValue);
    else
      fprintf(stdout, "%s\n", TokenNames.at(tok).c_str());
  }
  return 0;
}
```

For `tok_name` and `tok_number`, the token alone is not enough, so I also print the name text or numeric value.

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

I can see that while comments do not produce tokens of their own, comment-only and blank lines still produce newline tokens. I'm okay with this behavior.

## `lit` or `llvm-lit` Quickstart

I use LLVM's `lit` test runner for all my tests. In each small `.pyxc` test file, I add a `# RUN:` line that specifies the command and how to check its result. Through `test/lit.cfg.py`, I configure `lit` to run every `.pyxc` file in a directory and report whether each one passes or fails.

`test/sample_chapter_1.pyxc` is the exact `add.pyxc` program from earlier in this chapter, wrapped in `# RUN:` lines:

```pyxc
# RUN: %pyxc < %s | grep -Fxq "'def'"
# RUN: %pyxc < %s | grep -Fxq "name: print"
# RUN: %pyxc < %s | grep -Fxq "number: 1"
# RUN: %pyxc < %s | grep -Fxq "number: 2"

# sample: the add.pyxc example from the doc, run end-to-end through the lexer.
# add.pyxc
def add(x, y): # define a function
    x + y # return the sum

print(add(1, 2)) # call the add function and print its value
```

I define `%pyxc` as a substitution in `test/lit.cfg.py`:

```python
config.substitutions.append(("%pyxc", os.path.join(chapter_dir, "build", "pyxc")))
```

I use it as the path of the binary I just built.

I do not define `%s` myself. `lit` supplies it as the path of the test file containing the `# RUN:` line.

So `%pyxc < %s` becomes something like `build/pyxc < test/sample_chapter_1.pyxc`.

I use `grep -Fxq` for three things:

1. `-F` treats the pattern as a literal string, not a regex; 
2. `-x` matches the whole line exactly, not just a substring somewhere in it; 
3. `-q` stays quiet and just sets the exit status, `0` if found, `1` if not, which is what `lit` checks to decide pass or fail.

Each `# RUN:` line follows the same pipeline:

1. Runs `%pyxc < %s`, i.e. `build/pyxc < test/sample_chapter_1.pyxc`
2. Pipes the output to `grep`, checking for one token I expect to see: the `def` keyword, `print` showing up as a plain `name` just as any function name will in my scheme (even though it's currently an undefined function), or one of the two numbers, `1` and `2`.
3. `grep` exits `0` if it finds a match and `1` if it doesn't.
4. `lit` reads that exit code to decide pass or fail. 

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

I could chain the greps to a single `# RUN:` but then I wouldn't know what condition the test is failing on, so I'll do each test with a separate run. 

The `test/` directory has lit tests covering each token type. I run the suite with:

```bash
llvm-lit test/
```

```text
-- Testing: 11 tests, 8 workers --
PASS: pyxc-chapter01 :: comment_discards.pyxc (1 of 11)
PASS: pyxc-chapter01 :: error_unknown_character.pyxc (2 of 11)
PASS: pyxc-chapter01 :: number_leading_dot.pyxc (3 of 11)
PASS: pyxc-chapter01 :: name_underscore.pyxc (4 of 11)
PASS: pyxc-chapter01 :: sample_chapter_1.pyxc (5 of 11)
PASS: pyxc-chapter01 :: number_integer.pyxc (6 of 11)
PASS: pyxc-chapter01 :: punctuation_tokens.pyxc (7 of 11)
PASS: pyxc-chapter01 :: number_decimal.pyxc (8 of 11)
PASS: pyxc-chapter01 :: comment_eof.pyxc (9 of 11)
PASS: pyxc-chapter01 :: keyword_def.pyxc (10 of 11)
PASS: pyxc-chapter01 :: name_simple.pyxc (11 of 11)

Testing Time: 0.31s

Total Discovered Tests: 11
  Passed: 11 (100.00%)
```

The test order and timing above are from one run; `lit` runs tests in parallel across several workers, so both vary from run to run.

## Try It

Earlier I mentioned that `1.23.45.67` lexes as a single number and silently drops the rest. Here it is for real:

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
