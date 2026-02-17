---
description: "Implement Python-style indentation handling with indent/dedent tokens and parser updates so block structure no longer relies on braces or explicit terminators."
---
# 14. Pyxc: Python-Style Indentation

This chapter adds Python 3–style indentation to the language. We move from newline‑only structure to explicit `indent`/`dedent` tokens, and wire a token stream printer so you can see how the lexer is shaping the input. Each section names the functions that changed and why.


!!!note
    To follow along you can download the code from GitHub [pyxc-llvm-tutorial](https://github.com/alankarmisra/pyxc-llvm-tutorial) or you can see the full source code here [code/chapter15](https://github.com/alankarmisra/pyxc-llvm-tutorial/tree/main/code/chapter15).

## Grammar (EBNF)

Chapter 15 keeps the same language constructs as Chapter 14, but changes block structure from newline-only behavior to explicit `indent`/`dedent` tokens.

```ebnf
program      = { top_item } , eof ;
top_item     = newline | function_def | extern_decl | statement ;

statement    = if_stmt | for_stmt | return_stmt | expr_stmt ;

if_stmt      = "if" , expression , ":" , suite ,
               "else" , ":" , suite ;
for_stmt     = "for" , identifier , "in" , "range" , "(" ,
               expression , "," , expression , [ "," , expression ] ,
               ")" , ":" , suite ;

suite        = inline_suite | block_suite ;
inline_suite = statement ;
block_suite  = newline , indent , statement_list , dedent ;
```

## Indentation rules (Python 3 style)

- A new block starts after `:` and is defined by **greater indentation** than its parent line.
- A block ends when indentation **decreases** to a previous indentation level.
- Indentation levels are **absolute** column widths; a dedent must match a previous level exactly.
- Blank lines are ignored (they do not start or end blocks).
- Tabs and spaces must **not be mixed** for indentation (we enforce one style for the module).
- At end‑of‑file, any open indentation levels are **implicitly closed** with `dedent` tokens.

## Tracking indentation with a stack + pending tokens

We track indentation levels in a stack (`Indents`) and accumulate `dedent` tokens in a small deque (`PendingTokens`). Each line start computes its leading whitespace and compares it to the top of the stack.

For example, consider this `fib.pyxc`:

```py
extern def printd(x)

def fib(x):
    if(x < 3):
        return 1
    else:
        return fib(x-1) + fib(x-2)
```

As we scan line by line, the indentation stack evolves like this (comments show the stack at each step):

```text
# start: [0]
"def fib": indent=0   -> [0]
"    if": indent=4    -> push 4      -> [0, 4]
"        return": 8   -> push 8      -> [0, 4, 8]
"    else": 4         -> pop 8       -> [0, 4]  (emit dedent)
"        return": 8   -> push 8      -> [0, 4, 8]
"<eof>":               -> pop to 0    -> [0]     (emit dedent, dedent)
```

This is why a **stack + queue** design works well: the stack records indentation levels, while the queue lets the lexer return `dedent` tokens one at a time.

## New tokens for indentation

**Where:** `enum Token`

We add three new tokens:

- `tok_indent` for the start of a block
- `tok_dedent` for the end of a block
- `tok_error` for indentation errors

These are used by the lexer and parser to preserve the block structure.

## Counting leading whitespace

**Where:** `countLeadingWhitespace()`

We scan the leading spaces/tabs at the start of each logical line. Tabs are expanded to the next tab stop, and we enforce a single indentation style per module (spaces **or** tabs).

```cpp
static int countLeadingWhitespace(int &LastChar) {
  int indentCount = 0;
  bool didSetIndent = false;

  while (true) {
    while (LastChar == ' ' || LastChar == '\t') {
      if (ModuleIndentType == -1) {
        didSetIndent = true;
        ModuleIndentType = LastChar;
      } else if (LastChar != ModuleIndentType) {
        LogError("You cannot mix tabs and spaces.");
        return -1;
      }
      indentCount += LastChar == '\t' ? 8 - (LexLoc.Col % 8) : 1;
      LastChar = advance();
    }

    if (LastChar == '\r' || LastChar == '\n') {
      if (didSetIndent) {
        didSetIndent = false;
        indentCount = 0;
        ModuleIndentType = -1;
      }
      LastChar = advance();
      continue;
    }

    break;
  }

  return indentCount;
}
```

**Why:** We need a reliable indentation width at line start, and we need to ignore blank lines entirely.

## Indent / dedent detection and emission

**Where:** `IsIndent()`, `IsDedent()`, `HandleIndent()`, `HandleDedent()`

We compare the computed `leadingWhitespace` against the top of the stack to decide whether to emit `indent`/`dedent` tokens.

```cpp
static bool IsIndent(int leadingWhitespace) {
  return leadingWhitespace > Indents.back();
}

static bool IsDedent(int leadingWhitespace) {
  return leadingWhitespace < Indents.back();
}
```

On indent, we push the new level and emit `tok_indent`. On dedent, we pop until we reach a prior level and queue `tok_dedent` tokens. If the dedent doesn’t match any prior level, we emit `tok_error`.

```cpp
static int HandleIndent(int leadingWhitespace) {
  Indents.push_back(leadingWhitespace);
  return tok_indent;
}

static int HandleDedent(int leadingWhitespace) {
  int dedents = 0;
  while (leadingWhitespace < Indents.back()) {
    Indents.pop_back();
    dedents++;
  }

  if (leadingWhitespace != Indents.back()) {
    LogError("Expected indentation.");
    Indents = {0};
    PendingTokens.clear();
    return tok_error;
  }

  while (dedents-- > 1) {
    PendingTokens.push_back(tok_dedent);
  }
  return tok_dedent;
}
```

**Why:** This mirrors Python’s block model. A dedent must land exactly on a prior indentation level.

## EOF: draining open indents

**Where:** `DrainIndents()` and `gettok()`

At EOF we emit any remaining dedents (implicitly closing all open blocks).

```cpp
static int DrainIndents() {
  int dedents = 0;
  while (Indents.size() > 1) {
    Indents.pop_back();
    dedents++;
  }

  if (dedents > 0) {
    while (dedents-- > 1)
      PendingTokens.push_back(tok_dedent);
    return tok_dedent;
  }

  return tok_eof;
}
```

**Why:** Python implicitly closes open blocks at EOF. This ensures the parser sees the dedents.

## The lexer control flow

**Where:** `gettok()`

We added:

- A **line‑start** indentation phase (before regular token scanning)
- A **pending token** phase (dedents queued from a previous line)
- EOF handling via `DrainIndents()`

The indentation phase calls `countLeadingWhitespace()` and then uses `IsIndent`/`IsDedent` to emit tokens.

## Token printing for debugging

**Where:** `TokenName()` and `PrintTokens()`

A token stream printer makes it much easier to debug indentation. It prints one line of tokens per source line, including explicit `<indent=...>` and `<dedent>` tokens and a trailing `<eof>`.

```cpp
static const char *TokenName(int Tok) { /* ... */ }

static void PrintTokens(const std::string &filename) {
  int Tok = gettok();
  bool FirstOnLine = true;

  while (Tok != tok_eof) {
    if (Tok == tok_eol) {
      fprintf(stderr, "<eol>\n");
      FirstOnLine = true;
      Tok = gettok();
      continue;
    }

    if (!FirstOnLine)
      fprintf(stderr, " ");
    FirstOnLine = false;

    if (Tok == tok_indent) {
      fprintf(stderr, "<indent=%d>", LastIndentWidth);
    } else {
      const char *Name = TokenName(Tok);
      if (Name)
        fprintf(stderr, "%s", Name);
      else if (isascii(Tok))
        fprintf(stderr, "<%c>", Tok);
      else
        fprintf(stderr, "<tok=%d>", Tok);
    }

    Tok = gettok();
  }

  if (!FirstOnLine)
    fprintf(stderr, " ");
  fprintf(stderr, "<eof>\n");
}
```

**Why:** When debugging indentation, seeing explicit `indent`/`dedent` tokens is the fastest way to find mismatches between the lexer and the parser.

## Command‑line mode: -t (tokens)

**Where:** `ExecutionMode` enum + `main()`

We add a `-t` mode to print the token stream for a file:

```bash
./pyxc fib.pyxc -t
```

```bash
<extern> <def> <identifier> <(> <identifier> <)><eol>
<def> <identifier> <(> <identifier> <)> <:><eol>
<indent=4> <if> <(> <identifier> <<> <number> <)> <:><eol>
<indent=8> <return> <number><eol>
<dedent> <else> <:><eol>
<indent=8> <return> <identifier> <(> <identifier> <-> <number> <)> <+> <identifier> <(> <identifier> <-> <number> <)><eol>
<dedent> <dedent> <def> <identifier> <(> <)> <:><eol>
<indent=4> <return> <identifier> <(> <identifier> <(> <number> <)> <)><eol>
<dedent> <identifier> <(> <)><eol>
<eof>
```

This pairs naturally with `PrintTokens()` and gives a fast, deterministic view of how the lexer is classifying the input.

## Compile and Run

Use the same build setup style from Chapter 14.

Run:

```bash
cd code/chapter15 && ./build.sh
```

## Conclusion
Next, we'll handle multi‑expression blocks which will remove some of the weirdness of our rather limited single-expression syntax and allow us to write more expressive programs.

## Compile / Run / Test (Hands-on)

Build this chapter:

```bash
cd code/chapter15 && ./build.sh
```

Run one sample program:

```bash
code/chapter15/pyxc -i code/chapter15/fib.pyxc
```

Run the chapter tests (when a test suite exists):

```bash
cd code/chapter15/test
lit -sv .
```

Try editing a test or two and see how quickly you can predict the outcome.

When you're done, clean artifacts:

```bash
cd code/chapter15 && ./build.sh
```


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
