---
description: "Add file input mode and a -v IR flag so pyxc can execute source files through the same JIT pipeline as the REPL."
---
# 9. pyxc: File Input Mode

## What I Am Building

There comes a point in any command-line existence when you realize you have typed `def add(a, b): a + b` into a terminal for the fortieth time, by hand, like some kind of medieval scribe, and that this is not a life. Real programming languages have long since worked out that you can simply put your code in a file and hand it to the computer all at once, an innovation apparently overlooked by whoever designed the REPL I've been living in since Chapter 2. This chapter fixes that oversight, and not a moment too soon.

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
cd pyxc-llvm-tutorial/code/chapter-09
```

## One `FILE*` for Both Modes

The C input API represents both standard input and an open file as a `FILE*`. I make the lexer read through an `Input` variable so I can point it at either source. These two globals sit right after `OptLevel` (from [Chapter 8](chapter-08.md)), just before the lexer section:

```cppdiff
*static cl::opt<unsigned> OptLevel("O", cl::desc("Optimization level"),
*                                  cl::value_desc("0|1|2|3"), cl::Prefix,
*                                  cl::init(2), cl::cat(PyxcCategory));
*
+static FILE *Input = stdin;
+static bool IsRepl = true;
*
*//===----------------------------------------===//
*// Lexer
*//===----------------------------------------===//
```

I start with `Input` pointing to `stdin`. If I receive a filename, I open it and replace that pointer. The lexer always reads from `Input`, so I do not need separate lexer logic for the two modes.

```cppdiff
*static int advance() {
-  int LastChar = getchar();
+  int LastChar = fgetc(Input);
*
*  // case: '\r' or '\r\n'
*  if (LastChar == '\r') {
-    int NextChar = getchar();
+    int NextChar = fgetc(Input);
*
*    // A following '\n' is part of the same line ending; eat it.
*    // Anything else belongs to the next token; put it back.
*    // (EOF can't be put back at all, so it's excluded from that check.
*    // The next getchar() will still return EOF, so we don't lose it.)
*    if (NextChar != '\n' && NextChar != EOF)
-      ungetc(NextChar, stdin);
+      ungetc(NextChar, Input);
*    PyxcSourceManager.onChar('\n');
*    LexerLocation.Line++;
*    LexerLocation.Column = 0;
*    return '\n';
*  }
*  ...
*}
```

Changing the pointer changes the source of every later character read.

## Parsing the Command Line

LLVM already requires me to process `-O` through its command-line library. I add two more options to the same setup: an optional filename and `-v`.

### Selecting the Input Mode

`InputFile` goes right after `PyxcCategory`, before `OptLevel` (from [Chapter 8](chapter-08.md)):

```cppdiff
*static cl::OptionCategory PyxcCategory("Pyxc options");
*
+// Optional positional input: 0 args => REPL, 1 arg => file mode.
+static cl::opt<string> InputFile(cl::Positional, cl::desc("[script.pyxc]"),
+                                      cl::init(""), cl::cat(PyxcCategory));
*...
*static cl::opt<unsigned> OptLevel("O", cl::desc("Optimization level"),
*                                  cl::value_desc("0|1|2|3"), cl::Prefix,
*                                  cl::init(2), cl::cat(PyxcCategory));
```

I mark `InputFile` as positional so a bare argument such as `program.pyxc` fills it. I use an empty string as the default, which means no filename was provided.

```cppdiff
*int ProcessCommandLine(int argc, const char **argv) {
*  cl::HideUnrelatedOptions(PyxcCategory);
*  cl::ParseCommandLineOptions(argc, argv, "pyxc\n");
*
*  if (OptLevel > 3) {
*    fprintf(stderr, "Error: -O level must be 0, 1, 2, or 3\n");
*    return -1;
*  }
*
+  if (!InputFile.empty()) {
+    Input = fopen(InputFile.c_str(), "r");
+    if (!Input) {
+      perror(InputFile.c_str());
+      return -1;
+    }
+    IsRepl = false;
+  } else {
+    IsRepl = true;
+  }
+
*  return 0;
*}
```

If I have a filename, I open it, point `Input` at the resulting handle, and disable REPL output. Otherwise, I keep reading from `stdin`.

When `fopen` fails, it sets `errno`. I use `perror` to print the filename followed by the operating system's error description.

### Selecting IR Output

`VerboseIR` goes right after `InputFile`, before `OptLevel`:

```cppdiff
*static cl::opt<string> InputFile(cl::Positional, cl::desc("[script.pyxc]"),
*                                      cl::init(""), cl::cat(PyxcCategory));
*
+// Verbose IR dump in both REPL and file mode.
+static cl::opt<bool> VerboseIR("v",
+                               cl::desc("Print generated LLVM IR to stderr"),
+                               cl::init(false), cl::cat(PyxcCategory));
*
*static cl::opt<unsigned> OptLevel("O", cl::desc("Optimization level"),
*                                  cl::value_desc("0|1|2|3"), cl::Prefix,
*                                  cl::init(2), cl::cat(PyxcCategory));
```

I register `VerboseIR` as a Boolean option named `v`. It remains false unless I pass `-v`.

## Suppressing REPL Noise in File Mode

I suppress output that belongs to an interactive session when I run a file:

- `ready>` prompts
- `Parsed a function definition.` / `Parsed an extern.` / `Parsed a top-level expression.` confirmations
- `Evaluated to ...` after each expression

I centralize the repeated `IsRepl` check in two helpers I already introduced in [Chapter 8](chapter-08.md):

```cppdiff
*void PrintReplPrompt() {
+  if (IsRepl) {
*    fflush(stdout);
*    fprintf(stderr, "ready> ");
+  }
*}
```

```cppdiff
*void Log(const string &message) {
+  if (IsRepl)
*    fprintf(stderr, "%s", message.c_str());
*}
```

I apply the same check to the automatic result, at the end of `HandleTopLevelExpression()`:

```cppdiff
*static void HandleTopLevelExpression() {
*  ...
*  if (auto *FunctionIR = FunctionDefinition->codegen()) {
*    ...
*    double (*FP)() = ExprSymbol.toPtr<double (*)()>();
*    double result = FP();
+    if (IsRepl)
*      PrintEvaluationResult(result);
*    ...
*  }
*}
```

In file mode, the program only produces output that the pyxc source requests through functions such as `printd` and `putchard`.

## Printing IR with -v

Each handler checks `VerboseIR` before printing generated IR:

```cppdiff
*static void HandleFunctionDefinition() {
*  ...
*  Log("Parsed a function definition.\n");
+  if (VerboseIR)
*    FunctionIR->print(errs());
*  ...
*}
```

```cppdiff
*static void HandleExtern() {
*  ...
*  Log("Parsed an extern.\n");
+  if (VerboseIR)
*    FunctionIR->print(errs());
*  ...
*}
```

```cppdiff
*static void HandleTopLevelExpression() {
*  ...
*  Log("Parsed a top-level expression.\n");
+  if (VerboseIR)
*    FunctionIR->print(errs());
*  ...
*}
```

After `MainLoop()` finishes, I close an input file that I opened, at the end of `main()`:

```cppdiff
*int main(int argc, const char **argv) {
*  ...
*  MainLoop();
*
+  if (Input && Input != stdin) {
+    fclose(Input);
+    Input = stdin;
+  }
+
*  return 0;
*}
```

I check `Input != stdin` because I do not own standard input and must not close it. I reset the pointer after closing the file so it no longer refers to a closed handle.

## Build and Run

```bash
cd code/chapter-09
cmake -S . -B build && cmake --build build
```

```bash
./build/pyxc -v
```

or

```bash
./build/pyxc filename.pyxc -v
```

```bash
llvm-lit -v test/
```

## Try It

If the filename I pass doesn't exist, `fopen` fails and `perror` reports it using the operating system's own error text:

<!-- code-merge:start -->
```bash
$ build/pyxc nosuchfile.pyxc
```
```text
nosuchfile.pyxc: No such file or directory
```
<!-- code-merge:end -->

`ProcessCommandLine` returns `-1` in this case, so `main` never reaches `MainLoop()` at all.

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

I'll help you figure it out.
