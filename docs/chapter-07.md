---
description: "Add file input mode and a -v IR flag so pyxc can execute source files through the same JIT pipeline as the REPL."
---
# 7. Pyxc: File Input Mode

## Where We Are

[Chapter 6](chapter-06.md) added a JIT that evaluates expressions immediately. But there's no way to run a source file — you have to type everything into the REPL. There's also no way to inspect the generated IR.

At the end of this chapter we'll be able to pass a filename argument to pyxc like so:

```bash
$ build/pyxc test/file_mode.pyxc
7.000000
```

In addition, we will introduce a new switch, `-v` which will output the IR:

<!-- code-merge:start -->
```bash
$ build/pyxc test/file_mode.pyxc -v
```
```llvm
declare double @printd(double)
define double @add(double %a, double %b) {
entry:
  %addtmp = fadd double %a, %b
  ret double %addtmp
}
define double @__anon_expr() {
entry:
  %calltmp = call double @add(double 3.000000e+00, double 4.000000e+00)
  %calltmp1 = call double @printd(double %calltmp)
  ret double %calltmp1
}
```
```bash
7.000000
```
<!-- code-merge:end -->


The same flag works in the REPL:
<!-- code-merge:start -->
```bash
$ build/pyxc-v
```
```python
ready> def add(x, y): return x + y
```
```bash
Parsed a function definition.
```
```llvm
define double @add(double %a, double %b) {
entry:
  %addtmp = fadd double %a, %b
  ret double %addtmp
}
```
<!-- code-merge:end -->


## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-07
```

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

## Command-Line Parsing with LLVM's cl::

Chapter 6 added a `-O` switch to control the optimisation level. This chapter adds two more: a positional filename argument `InputFile` that makes `pyxc` run a source file instead of starting the REPL, and a `-v` flag, internally represented as `VerboseIR` that prints the generated IR to stderr.

**InputFile** and **IsRepl**

```cpp
static cl::OptionCategory PyxcCategory("Pyxc options");
...
// new option
static cl::opt<std::string> InputFile(cl::Positional, cl::desc("[script.pyxc]"),
                                      cl::init(""), cl::cat(PyxcCategory));
```

`cl::Positional` means the argument has no flag — it's just a bare filename on the command line. `cl::opt<std::string>` with `cl::init("")` defaults to an empty string when no file is given, so the check in `ProcessCommandLine` is simply `!InputFile.empty()`. However, to have a more descriptive variable, we introduce a global boolean variable `IsRepl` that stores the value of this test. 

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

**VerboseIR**

```cpp
// new option
static cl::opt<bool> VerboseIR("v",
                               cl::desc("Print generated LLVM IR to stderr"),
                               cl::init(false), cl::cat(PyxcCategory));
```

`cl::opt<bool> VerboseIR` with the name parameter set to `"v"` registers the `-v` flag. We use this parameter as is to determine if we want to emit the IR to stderr or not. 


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
```

```bash
./build/pyxc -v
```

or

```bash
./build/pyxc <filename> -v
```

## Known Limitations

- **Single file only.** The driver accepts at most one input file. Multiple-file compilation and a linker step come later.

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
