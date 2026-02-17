---
description: "Introduce a real command-line interface for pyxc with repl/run/build modes."
---
# 4. Pyxc: A better command line interface

## Introduction

Welcome to Chapter 4 of the [Pyxc: My First Language Frontend with LLVM](chapter-00.md) tutorial.

In [Chapter 2](chapter-02.md), we built a working recursive-descent parser and AST, and while doing that we quietly dropped the simple token-printing loop from Chapter 1 because parsing was the bigger priority at that point. This chapter is where we bring that back, but in a cleaner way, with real command-line switches so we can run `pyxc` in different modes without constantly rewriting `main()` every time we add a new capability.

## Source Code

To follow along you can download the code from GitHub [pyxc-llvm-tutorial](https://github.com/alankarmisra/pyxc-llvm-tutorial) or you can see the full source code here [code/chapter04](https://github.com/alankarmisra/pyxc-llvm-tutorial/tree/main/code/chapter04).

## What options are we trying to add?

Before we write even a line of implementation code, it helps to lock in the command shapes we want, because that keeps us honest about what users should be able to type:

```bash
./pyxc repl [--emit-tokens] [--emit-llvm]
./pyxc run script.pyxc [--emit-llvm]
./pyxc build script.pyxc [--emit=llvm|obj|exe] [-g] [-O0|-O1|-O2|-O3]
```

And just as important, here is what those commands mean *in Chapter 4 specifically*:

- `repl --emit-tokens` should work now.
- `repl --emit-llvm` should be accepted, but prints a "haven't learnt this yet" message.
- `run ...` should parse arguments correctly, then print "haven't learnt this yet".
- `build ...` should parse arguments correctly, validate values like `-O`, then print "haven't learnt this yet".

So the goal here is not to pretend everything is fully implemented yet. The goal is to put a clean command structure in place now, and make sure the program gives clear, truthful feedback for features we have not built yet.

## Why use LLVM's command-line utility here?

You can absolutely parse `argv` by hand, and for tiny tools that is often fine, but LLVM already ships a really good command-line utility in `llvm/Support/CommandLine.h`. Since we are already building on LLVM, using `llvm::cl` keeps things consistent with the rest of the ecosystem and makes subcommands like `repl`, `run`, and `build` much less painful to wire up.

## Step 1: Define subcommands and flags

We start by declaring three subcommands, then attach each option to the command where it belongs.

```cpp
static llvm::cl::SubCommand ReplCommand("repl",
                                        "Start the interactive REPL");
static llvm::cl::SubCommand RunCommand("run", "Run a .pyxc script");
static llvm::cl::SubCommand BuildCommand("build", "Build a .pyxc script");
```

Next, we define flags for `repl` and `run`:

```cpp
static llvm::cl::opt<bool>
    ReplEmitTokens("emit-tokens", llvm::cl::sub(ReplCommand),
                   llvm::cl::desc("Print lexer tokens instead of parsing"),
                   llvm::cl::init(false));

static llvm::cl::opt<bool>
    ReplEmitLLVM("emit-llvm", llvm::cl::sub(ReplCommand),
                 llvm::cl::desc("Emit LLVM from REPL input"),
                 llvm::cl::init(false));

static llvm::cl::list<string>
    RunInputFiles(llvm::cl::Positional, llvm::cl::sub(RunCommand),
                  llvm::cl::desc("<script.pyxc>"), llvm::cl::ZeroOrMore);

static llvm::cl::opt<bool>
    RunEmitLLVM("emit-llvm", llvm::cl::sub(RunCommand),
                llvm::cl::desc("Emit LLVM for the script"), llvm::cl::init(false));
```

One small detail here is worth calling out. We could have used `cl::opt<string>` for a single positional file, which sounds natural at first, but when the file is missing LLVM exits early with a generic “not enough positional arguments” style message. We use `cl::list<string>` with `ZeroOrMore` instead, because that lets parsing continue and gives us a chance to print clearer messages ourselves, like “run requires a file name” and “run accepts only one file name.”

Here are the `build` options:

```cpp
enum BuildOutputKind { BuildEmitLLVM, BuildEmitObj, BuildEmitExe };

static llvm::cl::list<string>
    BuildInputFiles(llvm::cl::Positional, llvm::cl::sub(BuildCommand),
                    llvm::cl::desc("<script.pyxc>"), llvm::cl::ZeroOrMore);

static llvm::cl::opt<BuildOutputKind> BuildEmit(
    "emit", llvm::cl::sub(BuildCommand),
    llvm::cl::desc("Output kind for build"),
    llvm::cl::values(clEnumValN(BuildEmitLLVM, "llvm", "Emit LLVM IR"),
                     clEnumValN(BuildEmitObj, "obj", "Emit object file"),
                     clEnumValN(BuildEmitExe, "exe", "Emit executable")),
    llvm::cl::init(BuildEmitExe));

static llvm::cl::opt<bool> BuildDebug("g", llvm::cl::sub(BuildCommand),
                                      llvm::cl::desc("Emit debug info"),
                                      llvm::cl::init(false));

static llvm::cl::opt<unsigned> BuildOptLevel(
    "O", llvm::cl::sub(BuildCommand),
    llvm::cl::desc("Optimization level (use -O0..-O3)"), llvm::cl::Prefix,
    llvm::cl::init(0));
```

`cl::Prefix` is the piece that lets `-O2` parse as option `O` with value `2`.

## Step 2: Parse arguments and validate early

At the start of `main`, we parse once:

```cpp
llvm::cl::ParseCommandLineOptions(argc, argv, "pyxc chapter04\n");
```

Then we validate `-O` immediately:

```cpp
if (BuildOptLevel > 3) {
  fprintf(stderr, "Error: invalid optimization level -O%u (expected 0..3)\n",
          static_cast<unsigned>(BuildOptLevel));
  return 1;
}
```

Doing this early keeps mistakes obvious and avoids silently accepting garbage values that only fail later in confusing ways.

## Step 3: Handle `run` and `build` (argument checks + honest stub)

For `run`, we validate file count first, then print the chapter-appropriate message:

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
  (void)RunEmitLLVM;
  fprintf(stderr, "run: i havent learnt how to do that yet.\n");
  return 1;
}
```

`build` follows the exact same pattern:

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

We will wire up the full behavior in later chapters, but the interface is already in place and predictable.

## Step 4: Add token mode to `repl`

When `repl --emit-tokens` is passed, we run a token-printing loop:

```cpp
static void EmitTokenStream() {
  fprintf(stderr, "ready> ");
  while (true) {
    int Tok = gettok();
    if (Tok == tok_eof)
      return;

    printf("%s", GetTokenName(Tok).c_str());
    if (Tok == tok_eol)
      printf("\nready> ");
    else
      printf(" ");
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

This brings back the Chapter 1 token view, which is still one of the easiest ways to sanity-check lexer behavior while we keep building.

## Step 5: Keep unsupported `repl --emit-llvm` explicit

We still accept the option, but we do not fake support for it yet:

```cpp
if (ReplCommand && ReplEmitLLVM) {
  fprintf(stderr, "repl --emit-llvm: i havent learnt how to do that yet.\n");
}
```

After printing that message, normal REPL startup still runs.

## Compiling

```bash
cd code/chapter04 && \
    cmake -S . -B build && \
    cmake --build build
```

### macOS / Linux shortcut

```bash
cd code/chapter04 && ./build.sh
```

## Sample Interaction

Here is a quick run-through of what Chapter 4 can do right now:

```bash
$ ./build/pyxc repl --emit-tokens
ready> def fib(a): return a + 10
'def' identifier '(' identifier ')' ':' 'return' identifier '+' number newline
ready>
```

```bash
$ ./build/pyxc repl --emit-llvm
repl --emit-llvm: i havent learnt how to do that yet.
ready>
```

```bash
$ ./build/pyxc run
Error: run requires a file name.
```

```bash
$ ./build/pyxc build --emit=llvm
Error: build requires a file name.
```

```bash
$ ./build/pyxc build test/def_simple.pyxc -O9
Error: invalid optimization level -O9 (expected 0..3)
```

## Conclusion

By the end of Chapter 4, you have:

- a clear command structure (`repl`, `run`, `build`),
- chapter-appropriate behavior for incomplete commands,
- token streaming in REPL (`--emit-tokens`),
- validation for required file names and `-O` values,
- and cleaner newline handling for diagnostics.

That gives us a solid base for the next chapter, where we start replacing these placeholders with real output behavior and real IR flow.

Next we will learn how to transform the Abstract Syntax Tree built in [Chapter 2](chapter-02.md), into LLVM IR. 

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
