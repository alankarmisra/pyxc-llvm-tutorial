---
description: "Add a real command-line interface with subcommands for repl, run, and build modes."
---
# 4. Pyxc: Command Line Interface

## What We're Building

In Chapter 2, we built a parser. To test it, we ran a hardcoded main loop. Every time we wanted to change behavior (print tokens vs parse vs generate IR), we had to rewrite `main()`.

That's fine for learning, but awkward for actual use. This chapter adds a proper command-line interface:

```bash
./pyxc repl [--emit=tokens|llvm-ir]
./pyxc run script.pyxc [--emit=llvm-ir]
./pyxc build script.pyxc [--emit=llvm-ir|obj|exe] [-g] [-O0|-O1|-O2|-O3]
```

We'll implement the parts we can do right now (`repl --emit=tokens`) and set up the structure for features we'll add later (`run` and `build`).

## Source Code

Grab the code: [code/chapter-04](https://github.com/alankarmisra/pyxc-llvm-tutorial/tree/main/code/chapter-04)

Or clone the whole repo:
```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-04
```

## Why LLVM's Command Line Library?

You can parse `argv` by hand, but LLVM already ships a solid command-line parser in `llvm/Support/CommandLine.h`. Since we're building on LLVM anyway, using `llvm::cl` gives us:

- Automatic help text
- Subcommands
- Validation
- Consistent error messages

It's less code than rolling our own, and it integrates with the rest of LLVM's tools.

## Defining Subcommands

We want three modes: `repl`, `run`, and `build`. Each is a subcommand:

```cpp
static llvm::cl::SubCommand ReplCommand("repl",
                                        "Start the interactive REPL");
static llvm::cl::SubCommand RunCommand("run", "Run a .pyxc script");
static llvm::cl::SubCommand BuildCommand("build", "Build a .pyxc script");
static llvm::cl::OptionCategory PyxcCategory("Pyxc options");
```

`PyxcCategory` groups our flags so we can hide unrelated LLVM/internal options from `--help` output. Without this, users see a large list of LLVM flags that aren't relevant to Pyxc.

## REPL Options

The REPL needs two flags:

```cpp
static llvm::cl::opt<bool>
    ReplEmitTokens("emit-tokens", llvm::cl::sub(ReplCommand),
                   llvm::cl::desc("Print lexer tokens instead of parsing"),
                   llvm::cl::init(false), llvm::cl::cat(PyxcCategory));
static llvm::cl::alias ReplEmitTokensShort(
    "t",
    llvm::cl::desc("Alias for --emit=tokens"),
    llvm::cl::aliasopt(ReplEmitTokens), llvm::cl::cat(PyxcCategory));

static llvm::cl::opt<bool>
    ReplEmitIR("emit-ir", llvm::cl::sub(ReplCommand),
                 llvm::cl::desc("Emit LLVM IR from REPL input"),
                 llvm::cl::init(false), llvm::cl::cat(PyxcCategory));
static llvm::cl::alias ReplEmitIRShort(
    "l",
    llvm::cl::desc("Alias for --emit=llvm-ir"),
    llvm::cl::aliasopt(ReplEmitIR), llvm::cl::cat(PyxcCategory));
```

`llvm::cl::sub(ReplCommand)` attaches these options to the `repl` subcommand. They won't appear in `run` or `build`.

## Run Options

```cpp
static llvm::cl::list<string>
    RunInputFiles(llvm::cl::Positional, llvm::cl::sub(RunCommand),
                  llvm::cl::desc("<script.pyxc>"), llvm::cl::ZeroOrMore,
                  llvm::cl::cat(PyxcCategory));

static llvm::cl::opt<bool>
    RunEmitIR("emit-ir", llvm::cl::sub(RunCommand),
                llvm::cl::desc("Emit LLVM for the script"), llvm::cl::init(false),
                llvm::cl::cat(PyxcCategory));
```

We use `cl::list` instead of `cl::opt` for the filename. Why? Because if we use `opt` and the user forgets the filename, LLVM exits with a generic "not enough arguments" error. With `list` and `ZeroOrMore`, we can check if the list is empty and print our own clearer error message.

## Build Options

```cpp
enum BuildOutputKind { BuildEmitIR, BuildEmitObj, BuildEmitExe };

static llvm::cl::list<string>
    BuildInputFiles(llvm::cl::Positional, llvm::cl::sub(BuildCommand),
                    llvm::cl::desc("<script.pyxc>"), llvm::cl::ZeroOrMore,
                    llvm::cl::cat(PyxcCategory));

static llvm::cl::opt<BuildOutputKind> BuildEmit(
    "emit", llvm::cl::sub(BuildCommand),
    llvm::cl::desc("Output kind for build"),
    llvm::cl::values(clEnumValN(BuildEmitIR, "ir", "Emit LLVM IR"),
                     clEnumValN(BuildEmitObj, "obj", "Emit object file"),
                     clEnumValN(BuildEmitExe, "exe", "Emit executable")),
    llvm::cl::init(BuildEmitExe), llvm::cl::cat(PyxcCategory));

static llvm::cl::opt<bool> BuildDebug("g", llvm::cl::sub(BuildCommand),
                                      llvm::cl::desc("Emit debug info"),
                                      llvm::cl::init(false),
                                      llvm::cl::cat(PyxcCategory));

static llvm::cl::opt<unsigned> BuildOptLevel(
    "O", llvm::cl::sub(BuildCommand),
    llvm::cl::desc("Optimization level (use -O0..-O3)"), llvm::cl::Prefix,
    llvm::cl::init(0), llvm::cl::cat(PyxcCategory));
```

`cl::Prefix` lets `-O2` parse as option `O` with value `2`.

The `BuildEmit` option uses `cl::values` to define valid choices. LLVM will reject `--emit=garbage` automatically.

## Parsing Arguments

At the start of `main`:

```cpp
llvm::cl::HideUnrelatedOptions(PyxcCategory);
llvm::cl::HideUnrelatedOptions(PyxcCategory, ReplCommand);
llvm::cl::HideUnrelatedOptions(PyxcCategory, RunCommand);
llvm::cl::HideUnrelatedOptions(PyxcCategory, BuildCommand);
llvm::cl::ParseCommandLineOptions(argc, argv, "pyxc chapter04\n");
```

This hides unrelated LLVM-internal flags from normal help output, then parses the command line and populates all our option variables.

## Validating Early

Check `-O` immediately:

```cpp
if (BuildOptLevel > 3) {
  fprintf(stderr, "Error: invalid optimization level -O%u (expected 0..3)\n",
          static_cast<unsigned>(BuildOptLevel));
  return 1;
}
```

Catching mistakes early makes errors obvious.

## Handling Run (Stub)

```cpp
if (RunCommand) {
  if (RunInputFiles.empty()) {
    fprintf(stderr, "Error: run requires a file name.\n");
    return 1;
  }
  if (RunInputFiles.size() > 1) {
    fprintf(stderr, "Error: run accepts only one file name.\n");
    return 1;
  }
  const string &RunInputFile = RunInputFiles.front();
  (void)RunInputFile;
  (void)RunEmitIR;
  fprintf(stderr, "run: i havent learnt how to do that yet.\n");
  return 1;
}
```

We validate the arguments (exactly one filename required), then print an honest message. We'll wire up the real behavior in later chapters.

## Handling Build (Stub)

```cpp
if (BuildCommand) {
  if (BuildInputFiles.empty()) {
    fprintf(stderr, "Error: build requires a file name.\n");
    return 1;
  }
  if (BuildInputFiles.size() > 1) {
    fprintf(stderr, "Error: build accepts only one file name.\n");
    return 1;
  }
  const string &BuildInputFile = BuildInputFiles.front();
  (void)BuildInputFile;
  (void)BuildEmit;
  (void)BuildDebug;
  (void)BuildOptLevel;
  fprintf(stderr, "build: i havent learnt how to do that yet.\n");
  return 1;
}
```

Same pattern: validate arguments, print honest stub message.

## Token Mode for REPL

We bring back the Chapter 1 token-printing loop:

```cpp
static void EmitTokenStream() {
  fprintf(stderr, "ready> ");
  while (true) {
    int Tok = gettok();
    if (Tok == tok_eof)
      return;

    fprintf(stderr, "%s", GetTokenName(Tok).c_str());
    if (Tok == tok_eol)
      fprintf(stderr, "\nready> ");
    else
      fprintf(stderr, " ");
  }
}
```

And in `main`:

```cpp
if (ReplCommand && ReplEmitTokens) {
  EmitTokenStream();
  return 0;
}
```

Now `./pyxc repl --emit=tokens` prints tokens again.

## LLVM Mode for REPL (Stub)

```cpp
if (ReplCommand && ReplEmitIR) {
  fprintf(stderr, "repl --emit=llvm-ir: i havent learnt how to do that yet.\n");
}
```

We accept the option but don't fake support. The normal REPL still runs afterward.

## Default REPL

If no special flags are set:

```cpp
DiagSourceMgr.reset();
getNextToken();
MainLoop();
return 0;
```

This is the Chapter 2 parser loop.

## Compile and Run

```bash
cd code/chapter-04
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && cmake --build build
./build/pyxc
```

Or use the shortcut:
```bash
cd code/chapter-04
./build.sh
```

## Sample Session

Print tokens:
```bash
$ ./build/pyxc repl --emit=tokens
ready> def fib(a): return a + 10
'def' identifier '(' identifier ')' ':' 'return' identifier '+' number newline
ready> ^D
```

Try unsupported LLVM mode:
```bash
$ ./build/pyxc repl --emit=llvm-ir
repl --emit=llvm-ir: i havent learnt how to do that yet.
ready> ^D
```

Try run without filename:
```bash
$ ./build/pyxc run
Error: run requires a file name.
```

Try build without filename:
```bash
$ ./build/pyxc build --emit=llvm-ir
Error: build requires a file name.
```

Try invalid optimization level:
```bash
$ ./build/pyxc build test/def_simple.pyxc -O9
Error: invalid optimization level -O9 (expected 0..3)
```

## Automated Testing with LLVM lit

Now that we have a CLI, we can write automated tests. This chapter includes 30 test files that verify the parser works correctly.

### What is lit?

`lit` (LLVM Integrated Tester) is the testing tool used by LLVM itself. It's a Python-based test runner that:

- Finds test files (`.pyxc` files in our case)
- Runs commands specified in the test files
- Checks the output matches expectations
- Reports pass/fail for each test

### Do You Have lit?

Check if you have `lit` or `llvm-lit` installed:

```bash
which lit
# or
which llvm-lit
```

If you don't have it, you have two options:

**Option 1: Install via pip**
```bash
pip install lit
```
This installs the command as `lit`.

**Option 2: Use LLVM's version (if you built LLVM from source in Chapter 3)**
```bash
# It's in your LLVM build directory as llvm-lit
export PATH=/path/to/llvm-project/build/bin:$PATH
```
This provides the command as `llvm-lit`.

Both work identically. In this tutorial, we'll use `lit` for brevity, but `llvm-lit` works the same way.

### Running the Tests

From the chapter04 directory:

```bash
cd code/chapter-04
cmake -S . -B build && cmake --build build
cd test
lit -v .
# or: llvm-lit -v .
```

You should see output like:

```text
-- Testing: 30 tests, 8 workers --
PASS: pyxc-chapter02 :: def_simple.pyxc (1 of 30)
PASS: pyxc-chapter02 :: def_multiarg.pyxc (2 of 30)
PASS: pyxc-chapter02 :: call_simple.pyxc (3 of 30)
...
Testing Time: 0.45s
  Passed: 30
```

All tests should pass!

**Note:** Use `lit` if you installed via pip, or `llvm-lit` if you built LLVM from source. Both commands work identically.

### How lit Tests Work

Let's look at a simple test file: `test/def_simple.pyxc`

```python
# RUN: %pyxc < %s > %t 2>&1
# RUN: grep -q "Parsed a function definition" %t

# Test simple function definition
def foo(x): return x
```

The magic happens in the `# RUN:` lines:

1. **`%pyxc`** - Replaced with the path to our compiler (defined in `lit.cfg.py`)
2. **`%s`** - Replaced with the test file itself (`def_simple.pyxc`)
3. **`%t`** - A temporary file for output
4. **`< %s`** - Feed the test file as stdin to pyxc
5. **`> %t 2>&1`** - Redirect stdout and stderr to temp file
6. **`grep -q`** - Search for expected output (exits 0 if found, 1 if not)

If all `RUN` commands exit successfully (exit code 0), the test passes.

### Test Categories

Our test suite covers:

**Basic Parsing (9 tests)**
- `def_simple.pyxc` - Simple function definition
- `def_multiarg.pyxc` - Multiple parameters
- `def_zero_args.pyxc` - No parameters
- `extern_simple.pyxc` - External declarations
- `top_expr.pyxc` - Top-level expressions

**Function Calls (3 tests)**
- `call_simple.pyxc` - Basic function calls
- `call_nested.pyxc` - Nested calls like `f(g(x))`
- `call_in_expr.pyxc` - Calls in expressions

**Expressions (4 tests)**
- `expr_basic_arithmetic.pyxc` - `+`, `-`, `*`, `/`
- `expr_precedence.pyxc` - Operator precedence
- `expr_comparison.pyxc` - `<` and `>`
- `expr_parentheses.pyxc` - Grouping with `()`

**Error Handling (6 tests)**
- `error_missing_colon.pyxc` - Catches missing `:`
- `error_missing_return.pyxc` - Catches missing `return`
- `error_bad_call_args.pyxc` - Malformed arguments
- `parse_error.pyxc` - General parse errors
- And more...

**Comments and Identifiers (5 tests)**
- `comment_full_line.pyxc` - Full line comments
- `comment_inline.pyxc` - End-of-line comments
- `identifier_underscore.pyxc` - Names with `_`
- `number_float.pyxc` - Floating point literals
- `number_integer.pyxc` - Integer literals

### Looking at an Error Test

Here's `test/error_bad_call_args.pyxc`:

```python
# RUN: %pyxc < %s > %t 2>&1
# RUN: grep -q "Error (" %t
# RUN: grep -q "Expected ')' or ',' in argument list" %t
# RUN: grep -q "\\^~~~" %t

# Test malformed function call arguments
foo(1 2)
```

This test **expects** an error! It verifies:
1. An error message appears
2. The specific error text is correct
3. The caret pointer (`^~~~`) appears in the output

This ensures our error messages stay helpful as we modify the code.

### The lit Configuration File

The file `test/lit.cfg.py` tells lit how to run tests:

```python
import os
import lit.formats

config.name = "pyxc-chapter02"
config.test_format = lit.formats.ShTest(True)
config.suffixes = [".pyxc"]
config.test_source_root = os.path.dirname(__file__)
config.test_exec_root = config.test_source_root

chapter_dir = os.path.abspath(os.path.join(config.test_source_root, ".."))
config.substitutions.append(("%pyxc", os.path.join(chapter_dir, "build", "pyxc")))
```

Key parts:
- `test_format = ShTest(True)` - Run shell commands from `# RUN:` lines
- `suffixes = [".pyxc"]` - Only test `.pyxc` files
- `substitutions.append(("%pyxc", ...))` - Replace `%pyxc` with path to our binary

### Tests Are Living Documentation

**Important:** The `test/` directory is the best way to see what the language can do at each chapter!

Want to know if nested function calls work? Check `test/call_nested.pyxc`.

Want to see what error messages look like? Check the `error_*.pyxc` tests.

**From now on, every chapter's test directory shows exactly what works.**

### Adding Your Own Tests

Try adding a test! Create `test/my_test.pyxc`:

```python
# RUN: %pyxc < %s > %t 2>&1
# RUN: grep -q "Parsed a function definition" %t

def my_func(a, b, c): return a + b + c
```

Run lit again:

```bash
cd test
lit -v my_test.pyxc
# or: llvm-lit -v my_test.pyxc
```

It should pass!

### Running Specific Tests

Run just one test:
```bash
lit -v def_simple.pyxc
```

Run tests matching a pattern:
```bash
lit -v error_*.pyxc
```

(Remember: use `llvm-lit` instead of `lit` if you built LLVM from source)

### What We Built

- **Subcommands** - `repl`, `run`, `build` with independent options
- **Validation** - Clear error messages for missing files and bad values
- **Token mode** - `repl --emit=tokens` for debugging the lexer
- **Honest stubs** - Unsupported features print clear messages
- **Test suite** - 30 automated tests covering parsing, expressions, and errors
- **lit integration** - Industry-standard testing infrastructure

The command-line interface is complete. Future chapters just need to replace the stub messages with real implementations.

## What's Next

In Chapter 5, we'll replace `repl --emit=llvm-ir: i havent learnt how to do that yet.` with actual LLVM IR generation from the AST.

## Need Help?

Stuck? Questions? Errors?

- **Issues:** [GitHub Issues](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [GitHub Discussions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)
- **Contribute:** Pull requests welcome!

Include:
- Chapter number
- Your OS/platform
- Full error message
- What you tried
