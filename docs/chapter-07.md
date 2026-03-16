---
description: "Add file input mode and a -v IR flag so pyxc can execute source files through the same JIT pipeline as the REPL."
---
# 7. Pyxc: File Input Mode

## Where We Are

[Chapter 6](chapter-06.md) added a JIT that evaluates expressions immediately. But there's no way to run a source file — you have to type everything into the REPL. There's also no way to inspect the generated IR without rebuilding the binary with a debug flag.

Before this chapter, passing a filename just drops you into the REPL:

```bash
$ build/pyxc test/test.pyxc
ready>
```

The filename argument is silently ignored.

After this chapter:

```bash
$ build/pyxc test/test.pyxc
30.000000
```

And with `-v`:

<!-- code-merge:start -->
```bash
$ build/pyxc test/test.pyxc -v
```
```llvm
declare double @printd(double)
define double @add(double %x, double %y) {
entry:
  %addtmp = fadd double %x, %y
  ret double %addtmp
}
define double @__anon_expr() {
entry:
  %calltmp = call double @add(double 1.000000e+01, double 2.000000e+01)
  %calltmp1 = call double @printd(double %calltmp)
  ret double %calltmp1
}
```
```bash
30.000000
```
<!-- code-merge:end -->

The same flag works in the REPL:

<!-- code-merge:start -->
```bash
$ build/pyxc -v
```
```python
ready> def add(x, y): return x + y
```
```bash
Parsed a function definition.
```
```llvm
define double @add(double %x, double %y) {
entry:
  %addtmp = fadd double %x, %y
  ret double %addtmp
}
```
```bash
ready>
```
<!-- code-merge:end -->

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-07
```

## Grammar

Chapter 7 adds newline awareness to the grammar. Previously the lexer returned `tok_eol` tokens but the parser never consumed them — they would cause spurious parse errors if they appeared between a `:` and the function body. This chapter formalises where newlines are allowed.

```ebnf
program         = [ eols ] [ top { eols top } ] [ eols ] ;
eols            = eol { eol } ;
top             = definition | external | toplevelexpr ;
definition      = "def" prototype ":" [ eols ] "return" expression ;
external        = "extern" "def" prototype ;
toplevelexpr    = expression ;
prototype       = identifier "(" [ identifier { "," identifier } ] ")" ;
expression      = primary binoprhs ;
binoprhs        = { binaryop primary } ;
primary         = identifierexpr | numberexpr | parenexpr ;
identifierexpr  = identifier | callexpr ;
callexpr        = identifier "(" [ expression { "," expression } ] ")" ;
numberexpr      = number ;
parenexpr       = "(" expression ")" ;
binaryop        = "+" | "-" | "*" ;
identifier      = (letter | "_") { letter | digit | "_" } ;
number          = digit { digit } [ "." { digit } ]
                | "." digit { digit } ;
letter          = "A".."Z" | "a".."z" ;
digit           = "0".."9" ;
eol             = "\r\n" | "\r" | "\n" ;
ws              = " " | "\t" ;
```

The key addition is `[ eols ]` in `definition`. It makes the newline between `:` and `return` optional, so both of these are valid:

```python
def add(x, y): return x + y       # single line

def add(x, y):                     # multi-line
    return x + y
```

`program` is also now defined to allow blank lines between top-level forms (`{ eols top }`), which is what makes a source file with blank lines between functions parse cleanly.

## One FILE* for Both Modes

The key insight is that `fgetc` doesn't care whether it reads from a terminal or a file — it just reads the next character from a `FILE*`. If we make the lexer's character source a `FILE*` variable instead of always using `stdin`, file mode is essentially free.

```cpp
static FILE *Input = stdin;
static bool IsRepl = true;
```

`Input` starts as `stdin`. When the user passes a filename, `main` opens the file and sets `Input` to that handle. The lexer calls `fgetc(Input)` everywhere — it never asks whether it's in REPL mode or file mode.

```cpp
static int advance() {
  int LastChar = fgetc(Input);
  // ...
}
```

That's the whole mechanism. One variable swap, and the existing lexer handles both cases without modification.

## Multi-Line Definitions

`consumeNewlines()` is a small helper that eats any consecutive `tok_eol` tokens:

```cpp
static void consumeNewlines() {
  while (CurTok == tok_eol)
    getNextToken();
}
```

`ParseDefinition` calls it after eating `:`, allowing the `return` expression to appear on the next line:

```cpp
if (CurTok != ':')
  return LogErrorF("Expected ':' in function definition");
getNextToken(); // eat ':'

consumeNewlines(); // allow body on next line

if (CurTok != tok_return)
  return LogErrorF("Expected 'return' in function body");
```

Without this call, a newline after `:` would leave `tok_eol` as the current token and the `tok_return` check would fail. With it, the parser skips past any newlines and lands on `return` regardless of whether the body is on the same line or the next.

This is the same `consumeNewlines()` that chapter 8 reuses after `if:`, `else:`, and `for:`.

## Command-Line Parsing with LLVM's cl::

Chapter 6 added a `-O` switch to control the optimisation level. This chapter adds two more: a positional filename argument that makes `pyxc` run a source file instead of starting the REPL, and a `-v` flag that prints the generated IR to stderr.

```cpp
static cl::OptionCategory PyxcCategory("Pyxc options");
...
// new options
static cl::opt<std::string> InputFile(cl::Positional, cl::desc("[script.pyxc]"),
                                      cl::init(""), cl::cat(PyxcCategory));

static cl::opt<bool> VerboseIR("v",
                               cl::desc("Print generated LLVM IR to stderr"),
                               cl::init(false), cl::cat(PyxcCategory));
```

`cl::Positional` means the argument has no flag — it's just a bare filename on the command line. `cl::opt<std::string>` with `cl::init("")` defaults to an empty string when no file is given, so the check in `ProcessCommandLine` is simply `!InputFile.empty()`. `cl::opt<bool>` with the name `"v"` registers the `-v` flag.

```cpp
int ProcessCommandLine(int argc, const char **argv) {
  ...

  if (!InputFile.empty()) {
    Input = fopen(InputFile.c_str(), "r");
    if (!Input) {
      perror(InputFile.c_str());
      return -1;
    }
    IsRepl = false;
  } else {
    IsRepl = true;
  }

  ...
}
```

When a file is given, `fopen` opens it and `Input` is set to the resulting handle. `IsRepl` flips to `false`. When no file is given, everything stays at its default.

`perror` is the right tool for `fopen` failures. It reads `errno` — which `fopen` sets on failure — and prints a human-readable message:

```bash
$ build/pyxc nosuchfile.pyxc
nosuchfile.pyxc: No such file or directory
```

The string passed to `perror` is the label printed before the colon. The actual error description comes from `errno`.

## Suppressing REPL Noise in File Mode

The REPL prints several things that make no sense when running a file:

- `ready>` prompts
- `Parsed a function definition.` / `Parsed an extern.` / `Parsed a top-level expression.` confirmations
- `Evaluated to ...` after each expression

All of these are gated on `IsRepl`. Two helpers centralise the check:

```cpp
void PrintReplPrompt() {
  if (IsRepl)
    fprintf(stderr, "ready> ");
}

void Log(const string &message) {
  if (IsRepl)
    fprintf(stderr, "%s", message.c_str());
}
```

And in `HandleTopLevelExpression`, the evaluated result is suppressed the same way:

```cpp
double result = FP();
if (IsRepl)
  fprintf(stderr, "Evaluated to %f\n", result);
```

When `IsRepl` is `false`, none of that text appears. The only output from a file run is what the Pyxc program itself explicitly produces — calls to `printd`, `putchard`, or any other `extern def` function.

## The -v Flag

`VerboseIR` is a `cl::opt<bool>` that starts `false`. When the user passes `-v`, LLVM's parser sets it to `true`. The three handlers check it before printing IR:

```cpp
// In HandleDefinition:
if (VerboseIR)
  FnIR->print(errs());

// In HandleExtern:
if (VerboseIR)
  FnIR->print(errs());

// In HandleTopLevelExpression:
if (VerboseIR)
  FnIR->print(errs());
```

`errs()` is LLVM's wrapper around `stderr`. `FnIR->print(errs())` is the same IR dump the earlier chapters always printed — now it's just conditional.

The flag works in both modes. In file mode it lets you inspect what the compiler generated without modifying the source. In the REPL it lets you see IR as you type, which is useful when learning what each expression compiles to.

## Cleanup

When running a file, `Input` is a handle that needs to be closed. `main` does this after `MainLoop` returns:

```cpp
if (Input && Input != stdin) {
  fclose(Input);
  Input = stdin;
}
```

The `Input != stdin` guard avoids closing `stdin` in REPL mode. Resetting `Input` to `stdin` afterwards is defensive — it ensures that if anything runs after `MainLoop`, it doesn't use a stale or closed handle.

## Build and Run

```bash
cmake -S . -B build
cmake --build build
./build/pyxc
```

## Try It

### File mode

<!-- code-merge:start -->
```bash
$ cat test/test.pyxc
```
```python
extern def printd(x)

def add(x, y):
    return x + y

printd(add(10,20))
```
```bash
$ build/pyxc test/test.pyxc
30.000000
```
<!-- code-merge:end -->

### File mode with -v

<!-- code-merge:start -->
```bash
$ build/pyxc test/test.pyxc -v
```
```llvm
declare double @printd(double)
define double @add(double %x, double %y) {
entry:
  %addtmp = fadd double %x, %y
  ret double %addtmp
}
define double @__anon_expr() {
entry:
  %calltmp = call double @add(double 1.000000e+01, double 2.000000e+01)
  %calltmp1 = call double @printd(double %calltmp)
  ret double %calltmp1
}
```
```bash
30.000000
```
<!-- code-merge:end -->

The IR for `extern def printd` is a `declare` — no body, just the signature. The IR for `add` shows the optimised single `fadd`. The IR for the anonymous expression shows both call sites. Then `30.000000` is printed by `printd` when the JIT executes `__anon_expr`.

### REPL mode (unchanged)

<!-- code-merge:start -->
```bash
$ build/pyxc
```
```python
ready> extern def printd(d)
```
```bash
Parsed an extern.
```
```python
ready> def add(x, y): return x + y
```
```bash
Parsed a function definition.
```
```python
ready> printd(add(10, 20))
```
```bash
Parsed a top-level expression.
30.000000
Evaluated to 0.000000
ready>
```
<!-- code-merge:end -->

### REPL mode with -v

<!-- code-merge:start -->
```bash
$ build/pyxc -v
```
```python
ready> def add(x, y): return x + y
```
```bash
Parsed a function definition.
```
```llvm
define double @add(double %x, double %y) {
entry:
  %addtmp = fadd double %x, %y
  ret double %addtmp
}
```
```python
ready> add(10, 20)
```
```bash
Parsed a top-level expression.
```
```llvm
define double @__anon_expr() {
entry:
  %calltmp = call double @add(double 1.000000e+01, double 2.000000e+01)
  ret double %calltmp
}
```
```bash
Evaluated to 30.000000
ready>
```
<!-- code-merge:end -->

## What We Built

| Piece | What it does |
|---|---|
| `static FILE *Input` | Single character source; `stdin` by default, a file handle in file mode |
| `static bool IsRepl` | Guards all REPL-only output: prompts, parse confirmations, evaluated-to lines |
| `cl::opt<std::string> InputFile` | Positional optional argument; empty string means REPL, non-empty means file mode |
| `cl::opt<bool> VerboseIR` | `-v` flag; gates `FnIR->print(errs())` in all three handlers |
| `consumeNewlines()` | Eats consecutive `tok_eol` tokens; called after `:` to allow the body on the next line |
| `ProcessCommandLine` | Opens the file, sets `Input` and `IsRepl` |
| `PrintReplPrompt` | Prints `ready> ` only when `IsRepl` is true |
| `Log` | Prints parse confirmation messages only when `IsRepl` is true |
| `fclose` / guard in `main` | Closes the input file handle after `MainLoop`; skips `stdin` |

## Known Limitations

- **No `--emit-ir` subcommand.** The `-v` flag prints IR alongside execution. A dedicated emit-only mode (no JIT, just IR to stdout) would be useful for piping into `opt` or `llc`. A later chapter can add it.
- **Single file only.** The driver accepts at most one input file. Multiple-file compilation and a linker step come later.
- **No control flow.** `if`/`else` and loops are not yet supported. Chapter 8 adds them.
- **No local variables.** `NamedValues` still only holds function parameters. Mutable locals require `alloca`/`store`/`load` and `mem2reg`. A later chapter adds these.

## What's Next

[Chapter 8](chapter-08.md) adds comparison operators (`==`, `!=`, `<=`, `>=`, `<`, `>`), `if`/`else` expressions, and `for` loops — giving Pyxc its first control flow and enough expressive power to render the Mandelbrot set.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.
