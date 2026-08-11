---
description: "Add file input mode and a -v IR flag so pyxc can execute source files through the same JIT pipeline as the REPL."
---
# 9. pyxc: File Input Mode

## Where I Am

In [Chapter 8](chapter-08.md), I added a JIT that evaluates expressions immediately. I can still only provide source through the REPL, so I now add file input.

After this change, I can pass a filename to `pyxc`:

```bash
$ build/pyxc test/file_mode.pyxc
7.000000
```

I also stop printing IR by default and add `-v` for cases where I want to inspect it:

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


I can use the same option in the REPL:
<!-- code-merge:start -->
```bash
$ build/pyxc -v
```
```pyxc
ready> def add(a, b): a + b
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
cd pyxc-llvm-tutorial/code/chapter-08
```

## One `FILE*` for Both Modes

The C input API represents both standard input and an open file as a `FILE*`. I make the lexer read through an `Input` variable so I can point it at either source:

```cpp
static FILE *Input = stdin;
static bool IsRepl = true;
```

I start with `Input` pointing to `stdin`. If I receive a filename, I open it and replace that pointer. The lexer always reads from `Input`, so I do not need separate lexer logic for the two modes.

```cpp
static int advance() {
  int LastChar = fgetc(Input);
  // ...
}
```

Changing the pointer changes the source of every later character read.

## Parsing the Command Line

LLVM already requires me to process `-O` through its command-line library. I add two more options to the same setup: an optional filename and `-v`.

### Selecting the Input Mode

```cpp
static cl::opt<std::string> InputFile(cl::Positional, cl::desc("[script.pyxc]"),
                                      cl::init(""), cl::cat(PyxcCategory));
```

I mark `InputFile` as positional so a bare argument such as `program.pyxc` fills it. I use an empty string as the default, which means no filename was provided.

```cpp
int ProcessCommandLine(int argc, const char **argv) {
  // ... parse LLVM's cl::opt flags, including -O ...
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

  return 0;
}
```

If I have a filename, I open it, point `Input` at the resulting handle, and disable REPL output. Otherwise, I keep reading from `stdin`.

When `fopen` fails, it sets `errno`. I use `perror` to print the filename followed by the operating system's error description:

```bash
$ build/pyxc nosuchfile.pyxc
nosuchfile.pyxc: No such file or directory
```

### Selecting IR Output

```cpp
static cl::opt<bool> VerboseIR("v",
                               cl::desc("Print generated LLVM IR to stderr"),
                               cl::init(false), cl::cat(PyxcCategory));
```

I register `VerboseIR` as a Boolean option named `v`. It remains false unless I pass `-v`.

## Suppressing REPL Noise in File Mode

I suppress output that belongs to an interactive session when I run a file:

- `ready>` prompts
- `Parsed a function definition.` / `Parsed an extern.` / `Parsed a top-level expression.` confirmations
- `Evaluated to ...` after each expression

I centralize the repeated `IsRepl` check in two helpers:

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

I apply the same check to the automatic result in `HandleTopLevelExpression()`:

```cpp
double result = FP();
if (IsRepl)
  fprintf(stderr, "Evaluated to %f\n", result);
```

In file mode, the program only produces output that the pyxc source requests through functions such as `printd` and `putchard`.

## Printing IR with -v

Each handler checks `VerboseIR` before printing generated IR:

```cpp
// In HandleFunctionDefinition():
if (VerboseIR)
  FnIR->print(errs());

// In HandleExtern:
if (VerboseIR)
  FnIR->print(errs());

// In HandleTopLevelExpression:
if (VerboseIR)
  FnIR->print(errs());
```

After `MainLoop()` finishes, I close an input file that I opened:

```cpp
if (Input && Input != stdin) {
  fclose(Input);
  Input = stdin;
}
```

I check `Input != stdin` because I do not own standard input and must not close it. I reset the pointer after closing the file so it no longer refers to a closed handle.

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
./build/pyxc filename.pyxc -v
```

## What's Next

[Chapter 10](chapter-10.md) adds comparisons, `if`/`else`, and `for` loops.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.
