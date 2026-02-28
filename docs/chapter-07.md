---
description: "Add file input mode and a -v IR flag so pyxc can execute source files through the same JIT pipeline as the REPL."
---
# 7. Pyxc: File Input Mode

## Where We Are

[Chapter 6](chapter-06.md) added a JIT that evaluates expressions immediately. But there's no way to run a source file — you have to type everything into the REPL. There's also no way to inspect the generated IR unless you rebuild the binary with a debug flag.

Before this chapter, running a file just drops you into the REPL:

```bash
$ build/pyxc test/test.pyxc
ready>
```

The filename argument is silently ignored. And there's no flag to see the IR.

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

The same flag works in the REPL too:

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

## One FILE* for Both Modes

The key insight is that `fgetc` doesn't care whether it reads from a terminal or a file — it just reads the next character from a `FILE*`. If we make the lexer's character source a `FILE*` variable instead of always using `stdin`, we get file mode for free.

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

## Command-Line Parsing with LLVM's cl::

LLVM ships a command-line parsing library, `llvm/Support/CommandLine.h`. We already use it for other LLVM tools in the build. For `pyxc` it replaces manual `argv` iteration with two declarations:

```cpp
static cl::OptionCategory PyxcCategory("Pyxc options");

// Optional positional argument: 0 => REPL, 1 => file mode.
static cl::list<std::string> InputFiles(cl::Positional,
                                        cl::desc("[script.pyxc]"),
                                        cl::ZeroOrMore,
                                        cl::cat(PyxcCategory));

// Verbose IR dump.
static cl::opt<bool> VerboseIR("v",
                               cl::desc("Print generated LLVM IR to stderr"),
                               cl::init(false),
                               cl::cat(PyxcCategory));
```

`cl::Positional` means the argument has no flag — it's just a bare filename on the command line. `cl::ZeroOrMore` means zero or one file is accepted (the driver enforces the "at most one" constraint explicitly). `cl::opt<bool>` with the name `"v"` registers the `-v` flag.

`cl::HideUnrelatedOptions` and `cl::ParseCommandLineOptions` in `ProcessCommandLine` do the actual parsing. LLVM handles `--help` automatically using the `cl::desc` strings.

```cpp
int ProcessCommandLine(int argc, const char **argv) {
  cl::HideUnrelatedOptions(PyxcCategory);
  cl::ParseCommandLineOptions(argc, argv, "pyxc\n");

  if (InputFiles.size() > 1) {
    fprintf(stderr, "Error: expected at most one input file.\n");
    return -1;
  }

  if (InputFiles.size() == 1) {
    Input = fopen(InputFiles[0].c_str(), "r");
    if (!Input) {
      perror(InputFiles[0].c_str());
      return -1;
    }
    IsRepl = false;
  } else {
    IsRepl = true;
  }

  return 0;
}
```

When a file is given, `fopen` opens it and `Input` is set to the resulting handle. `IsRepl` flips to `false`. When no file is given, everything stays at its default.

`perror` is the right tool for `fopen` failures. It reads `errno` — which `fopen` sets on failure — and prints a human-readable message:

```bash
$ build/pyxc nosuchfile.pyxc
nosuchfile.pyxc: No such file or directory
```

The string passed to `perror` is just the label printed before the colon. The actual error description comes from `errno`.

## Suppressing REPL Noise in File Mode

The REPL prints several things that make no sense when running a file:

- `ready>` prompts
- `Parsed a function definition.` / `Parsed an extern.` / `Parsed a top-level expression.` confirmations
- `Evaluated to ...` after each expression

All of these are gated on `IsRepl`. Two helpers centralise the check:

```cpp
void PrintConsoleReady() {
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
cd code/chapter-07
cmake -S . -B build && cmake --build build
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
| `cl::list<string> InputFiles` | Positional optional argument; zero entries means REPL, one means file mode |
| `cl::opt<bool> VerboseIR` | `-v` flag; gates `FnIR->print(errs())` in all three handlers |
| `ProcessCommandLine` | Opens the file, sets `Input` and `IsRepl`, validates argument count |
| `PrintConsoleReady` | Prints `ready> ` only when `IsRepl` is true |
| `Log` | Prints parse confirmation messages only when `IsRepl` is true |
| `fclose` / guard in `main` | Closes the input file handle after `MainLoop`; skips `stdin` |

## Known Limitations

- **No `--emit-ir` subcommand.** The `-v` flag prints IR alongside execution. A dedicated emit-only mode (no JIT, just IR to stdout) would be useful for piping into `opt` or `llc`. A later chapter can add it.
- **Single file only.** The driver enforces at most one input file. Multiple-file compilation and a linker step come later.
- **No control flow.** `if`/`else` and loops are not yet supported. A later chapter adds them.
- **No local variables.** `NamedValues` still only holds function parameters. Mutable locals require `alloca`/`store`/`load` and `mem2reg`. A later chapter adds these.

## What's Next

Chapter 8 adds comparison operators (`==`, `!=`, `<=`, `>=`) with correct precedence, giving Pyxc the building blocks it needs for control flow.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.
