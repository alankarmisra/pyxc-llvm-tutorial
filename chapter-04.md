---
description: "Add a real command-line interface with subcommands for repl, run, and build modes."
---
# 4. Pyxc: Command Line Interface

## What We're Building

In Chapter 2, we built a parser. To test it, we ran a hardcoded main loop. Every time we wanted to change behavior (print tokens vs parse vs generate IR), we had to rewrite `main()`.

That's fine for learning, but awkward for actual use. This chapter adds a proper command-line interface:

```bash
./pyxc repl [-t|--emit-tokens] [-l|--emit-llvm]
./pyxc run script.pyxc [--emit-llvm]
./pyxc build script.pyxc [--emit=llvm|obj|exe] [-g] [-O0|-O1|-O2|-O3]
```

We'll implement the parts we can do right now (`repl --emit-tokens`) and set up the structure for features we'll add later (`run` and `build`).

## Source Code

Grab the code: [code/chapter04](https://github.com/alankarmisra/pyxc-llvm-tutorial/tree/main/code/chapter04)

Or clone the whole repo:
```bash
git clone https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter04
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
```

## REPL Options

The REPL needs two flags:

```cpp
static llvm::cl::opt<bool>
    ReplEmitTokens("emit-tokens", llvm::cl::sub(ReplCommand),
                   llvm::cl::desc("Print lexer tokens instead of parsing"),
                   llvm::cl::init(false));
static llvm::cl::alias ReplEmitTokensShort(
    "t", llvm::cl::sub(ReplCommand),
    llvm::cl::desc("Alias for --emit-tokens"),
    llvm::cl::aliasopt(ReplEmitTokens));

static llvm::cl::opt<bool>
    ReplEmitLLVM("emit-llvm", llvm::cl::sub(ReplCommand),
                 llvm::cl::desc("Emit LLVM from REPL input"),
                 llvm::cl::init(false));
static llvm::cl::alias ReplEmitLLVMShort(
    "l", llvm::cl::sub(ReplCommand),
    llvm::cl::desc("Alias for --emit-llvm"),
    llvm::cl::aliasopt(ReplEmitLLVM));
```

`llvm::cl::sub(ReplCommand)` attaches these options to the `repl` subcommand. They won't appear in `run` or `build`.

## Run Options

```cpp
static llvm::cl::list<string>
    RunInputFiles(llvm::cl::Positional, llvm::cl::sub(RunCommand),
                  llvm::cl::desc("<script.pyxc>"), llvm::cl::ZeroOrMore);

static llvm::cl::opt<bool>
    RunEmitLLVM("emit-llvm", llvm::cl::sub(RunCommand),
                llvm::cl::desc("Emit LLVM for the script"), llvm::cl::init(false));
```

We use `cl::list` instead of `cl::opt` for the filename. Why? Because if we use `opt` and the user forgets the filename, LLVM exits with a generic "not enough arguments" error. With `list` and `ZeroOrMore`, we can check if the list is empty and print our own clearer error message.

## Build Options

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

`cl::Prefix` lets `-O2` parse as option `O` with value `2`.

The `BuildEmit` option uses `cl::values` to define valid choices. LLVM will reject `--emit=garbage` automatically.

## Parsing Arguments

At the start of `main`:

```cpp
llvm::cl::ParseCommandLineOptions(argc, argv, "pyxc chapter04\n");
```

This parses the command line and populates all our option variables.

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
  (void)RunEmitLLVM;
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

Now `./pyxc repl --emit-tokens` prints tokens again.

## LLVM Mode for REPL (Stub)

```cpp
if (ReplCommand && ReplEmitLLVM) {
  fprintf(stderr, "repl --emit-llvm: i havent learnt how to do that yet.\n");
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
cd code/chapter04
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && cmake --build build
./build/pyxc
```

Or use the shortcut:
```bash
cd code/chapter04
./build.sh
```

## Sample Session

Print tokens:
```bash
$ ./build/pyxc repl -t
ready> def fib(a): return a + 10
'def' identifier '(' identifier ')' ':' 'return' identifier '+' number newline
ready> ^D
```

Try unsupported LLVM mode:
```bash
$ ./build/pyxc repl -l
repl --emit-llvm: i havent learnt how to do that yet.
ready> ^D
```

Try run without filename:
```bash
$ ./build/pyxc run
Error: run requires a file name.
```

Try build without filename:
```bash
$ ./build/pyxc build --emit=llvm
Error: build requires a file name.
```

Try invalid optimization level:
```bash
$ ./build/pyxc build test/def_simple.pyxc -O9
Error: invalid optimization level -O9 (expected 0..3)
```

## What We Built

- **Subcommands** - `repl`, `run`, `build` with independent options
- **Validation** - Clear error messages for missing files and bad values
- **Token mode** - `repl --emit-tokens` for debugging the lexer
- **Honest stubs** - Unsupported features print clear messages

The command-line interface is complete. Future chapters just need to replace the stub messages with real implementations.

## What's Next

In Chapter 5, we'll replace `repl --emit-llvm: i havent learnt how to do that yet.` with actual LLVM IR generation from the AST.

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
