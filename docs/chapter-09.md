---
section: "LLVM and Execution"
description: "Run pyxc source files through the same lexer, parser, optimizer, and JIT used by the REPL."
---

# 9. pyxc: File Input Mode

Next: run a complete `.pyxc` file.

Chapter 8 always reads from `stdin`. Keep that interactive mode, but add a positional filename:

```bash
./build/pyxc program.pyxc
```

Do not build a second compiler path. The clean boundary is:

```text
stdin or file -> one FILE* -> the same frontend and JIT
```

Work in:

```bash
cd code/chapter-09
```

## 1. Add Shared Input State

Add:

```cpp
static FILE *Input = stdin;
static bool IsRepl = true;
```

`Input` identifies the character source. `IsRepl` controls presentation only: prompts and “Parsed...” chatter belong to an interactive session, not normal script output.

## 2. Route Every Character Read through `Input`

In `advance()`, replace:

```cpp
getchar()
```

with:

```cpp
fgetc(Input)
```

Replace newline lookahead reads the same way, and change:

```cpp
ungetc(NextChar, stdin)
```

to:

```cpp
ungetc(NextChar, Input)
```

This is the only lexer change required. Tokens, locations, diagnostics, parsing, codegen, optimization, and JIT execution remain shared.

## 3. Add the Positional Input Option

Near the existing LLVM command-line options, add:

```cpp
static cl::opt<string> InputFile(
    cl::Positional, cl::desc("[script.pyxc]"),
    cl::init(""), cl::cat(PyxcCategory));
```

This accepts zero or one positional filename:

```text
no filename -> REPL
filename    -> file mode
```

## 4. Add Optional IR Output

Chapter 8 always prints generated IR. Add:

```cpp
static cl::opt<bool> VerboseIR(
    "v", cl::desc("Print generated LLVM IR to stderr"),
    cl::init(false), cl::cat(PyxcCategory));
```

Wrap every successful IR dump:

```cpp
if (VerboseIR)
  FunctionIR->print(errs());
```

This makes normal program output usable while retaining an inspection mode:

```bash
./build/pyxc -v program.pyxc
```

Keep IR on `stderr`; runtime output remains on `stdout`.

## 5. Open the Selected Input

Extend `ProcessCommandLine()` after parsing and optimization-level validation:

```cpp
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
```

Use `perror()` so a missing file includes the operating system's explanation.

Do this before priming the lexer. `getNextToken()` must read its first character from the selected stream.

## 6. Centralize REPL-Only Output

Add:

```cpp
void PrintReplPrompt() {
  if (IsRepl) {
    fflush(stdout);
    fprintf(stderr, "ready> ");
  }
}
```

Replace direct prompt printing with `PrintReplPrompt()`.

Add a REPL-only logging helper:

```cpp
void Log(const string &Message) {
  if (IsRepl)
    fprintf(stderr, "%s", Message.c_str());
}
```

Replace confirmation messages such as:

```cpp
fprintf(stderr, "Parsed a function definition.\n");
```

with:

```cpp
Log("Parsed a function definition.\n");
```

Do the same for externs and top-level expressions.

Do not suppress diagnostics. Syntax, codegen, and file-opening errors must still be printed in both modes.

## 7. Keep Evaluation Output Explicit

Use one result function:

```cpp
void PrintEvaluationResult(double Result) {
  fprintf(stdout, "Evaluated to %f\n", Result);
}
```

Call it after executing a top-level anonymous expression.

This is program behavior, not parser chatter. Runtime functions such as `printd` and `putchard` also continue writing normally.

## 8. Close the File

After `MainLoop()` returns, add:

```cpp
if (Input && Input != stdin) {
  fclose(Input);
  Input = stdin;
}
```

Do not close `stdin`.

## 9. Build and Run the REPL

```bash
cmake -S . -B build \
  -DLLVM_DIR="$(llvm-config --cmakedir)"
cmake --build build
./build/pyxc
```

Try:

```pyxc
ready> 1 + 2
```

Expected:

```text
Parsed a top-level expression.
Evaluated to 3.000000
```

The REPL behavior should be unchanged except that IR is hidden unless `-v` is present.

## 10. Run a Source File

Create or use a test file such as:

```pyxc
extern def printd(x)
def square(x): x * x
printd(square(5))
```

Run:

```bash
./build/pyxc program.pyxc
```

Expected runtime output:

```text
25.000000
Evaluated to 0.000000
```

There should be no `ready>` prompt and no “Parsed...” messages.

Inspect the same run with IR:

```bash
./build/pyxc -v program.pyxc
```

Redirect the streams independently when useful:

```bash
./build/pyxc -v program.pyxc >program.out 2>program.ll
```

## 11. Test the Error Boundary

Run a missing file:

```bash
./build/pyxc does-not-exist.pyxc
```

Expected behavior:

```text
does-not-exist.pyxc: <operating-system error>
```

and a nonzero exit code.

Run the full suite:

```bash
llvm-lit -v test/
```

What you built is one compiler with two input presentations:

```text
REPL mode -> stdin + prompts + confirmations
file mode -> file  + quiet compiler presentation
both      -> same lexer/parser/codegen/optimizer/JIT
```

Next: [Chapter 10](chapter-10.md) adds comparisons, `if` expressions, and `for` loops.

## Need Help?

Build issues? Questions?

- [Report a problem with GitHub Issues](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- [Ask a question in GitHub Discussions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:

- Your operating system and version
- The chapter number
- The exact command you ran
- The complete error message
- The output of `c++ --version` and `cmake --version`
- The output of `llvm-config --version` for Chapter 6 and later
