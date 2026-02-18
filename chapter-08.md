---
description: "Implement object file emission and wire optimization levels into the build pipeline."
---
# 8. Pyxc: Object Files and Optimizations

## What We're Building

In Chapter 7, we built a JIT that could execute functions interactively. But we still had a stub for the `build` subcommand. This chapter makes `build` real:

```bash
./pyxc build fib.pyxc --emit=llvm-ir        # Emit LLVM IR to stdout
./pyxc build fib.pyxc --emit=obj         # Emit fib.o object file
./pyxc build fib.pyxc --emit=obj -O3     # Emit optimized object file
```

We'll also wire the `-O0` through `-O3` flags to actually control which optimization passes run.

## Source Code

Grab the code: [code/chapter08](https://github.com/alankarmisra/pyxc-llvm-tutorial/tree/main/code/chapter08)

Or clone the whole repo:
```bash
git clone https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter08
```

## The Problem

Chapter 7 let you run code interactively, but you couldn't:

1. Read a `.pyxc` file from disk and compile it
2. Emit a standalone object file you could link with other code
3. Control optimization level during compilation

This chapter fixes all three.

## New Runtime Flags

We need to track whether we're in interactive REPL mode or batch build mode. Add these globals in the Code Generation section:

```cpp
//===----------------------------------------------------------------------===//
// Code Generation
//===----------------------------------------------------------------------===//

static std::unique_ptr<LLVMContext> TheContext;
static std::unique_ptr<Module> TheModule;
static std::unique_ptr<IRBuilder<>> Builder;
static std::map<std::string, Value *> NamedValues;
static bool ShouldEmitIR = false;
static std::unique_ptr<PyxcJIT> TheJIT;
static std::unique_ptr<FunctionPassManager> TheFPM;
static std::unique_ptr<LoopAnalysisManager> TheLAM;
static std::unique_ptr<FunctionAnalysisManager> TheFAM;
static std::unique_ptr<CGSCCAnalysisManager> TheCGAM;
static std::unique_ptr<ModuleAnalysisManager> TheMAM;
static std::unique_ptr<PassInstrumentationCallbacks> ThePIC;
static std::unique_ptr<StandardInstrumentations> TheSI;
static ExitOnError ExitOnErr;
static bool InteractiveMode = true;           // <-- Add this
static bool BuildObjectMode = false;          // <-- Add this
static unsigned CurrentOptLevel = 0;          // <-- Add this
```

Also add an error flag in the lexer section:

```cpp
static std::string IdentifierStr; // Filled in if tok_identifier
static double NumVal;             // Filled in if tok_number
static std::string NumLiteralStr; // Filled in if tok_number
static bool HadError = false;     // <-- Add this
```

## Error Tracking

Update `LogError` to set the error flag. This lets us exit with non-zero status when compilation fails:

```cpp
/// LogError - Unified error reporting template.
template <typename T = void> T LogError(const char *Str) {
  HadError = true;  // <-- Add this line at the start
  SourceLocation DiagLoc = GetDiagnosticAnchorLoc(CurLoc, CurTok);
  fprintf(stderr, "%sError%s (Line: %d, Column: %d): %s\n", Red, Reset,
          DiagLoc.Line, DiagLoc.Col, Str);
  PrintErrorSourceContext(DiagLoc);
  if constexpr (std::is_void_v<T>)
    return;
  else if constexpr (std::is_pointer_v<T>)
    return nullptr;
  else
    return T{};
}
```

## Optimization Level Control

Replace the hardcoded pass setup in `InitializeModuleAndManagers()` with a switch on optimization level:

```cpp
static void InitializeModuleAndManagers() {
  // Open a new context and module.
  TheContext = std::make_unique<LLVMContext>();
  TheModule = std::make_unique<Module>("PyxcJIT", *TheContext);
  if (TheJIT)
    TheModule->setDataLayout(TheJIT->getDataLayout());

  // Create a new builder for the module.
  Builder = std::make_unique<IRBuilder<>>(*TheContext);

  // Create new pass and analysis managers.
  TheFPM = std::make_unique<FunctionPassManager>();
  TheLAM = std::make_unique<LoopAnalysisManager>();
  TheFAM = std::make_unique<FunctionAnalysisManager>();
  TheCGAM = std::make_unique<CGSCCAnalysisManager>();
  TheMAM = std::make_unique<ModuleAnalysisManager>();
  ThePIC = std::make_unique<PassInstrumentationCallbacks>();
  TheSI = std::make_unique<StandardInstrumentations>(*TheContext,
                                                     /*DebugLogging*/ false);
  TheSI->registerCallbacks(*ThePIC, TheMAM.get());

  // Add transform passes based on selected optimization level.
  switch (CurrentOptLevel) {
  case 0:
    break;
  case 1:
    TheFPM->addPass(InstCombinePass());
    TheFPM->addPass(ReassociatePass());
    break;
  case 2:
    TheFPM->addPass(InstCombinePass());
    TheFPM->addPass(ReassociatePass());
    TheFPM->addPass(GVNPass());
    break;
  default: // O3 and above
    TheFPM->addPass(InstCombinePass());
    TheFPM->addPass(ReassociatePass());
    TheFPM->addPass(GVNPass());
    TheFPM->addPass(SimplifyCFGPass());
    break;
  }

  // Register analysis passes.
  PassBuilder PB;
  PB.registerModuleAnalyses(*TheMAM);
  PB.registerFunctionAnalyses(*TheFAM);
  PB.crossRegisterProxies(*TheLAM, *TheFAM, *TheCGAM, *TheMAM);
}
```

This matches typical compiler behavior: `-O0` = no optimization, higher levels add progressively more passes.

## Build Mode Changes

In build mode, we don't want to JIT each function. We want to accumulate all functions in a single module, then run module-level optimizations, and finally emit an object file.

**Important:** We also skip per-function optimization during codegen when building objects. Instead, we'll run a complete module-level optimization pipeline later, right before code generation.

Update `FunctionAST::codegen()` to skip function passes in build mode:

```cpp
Function *FunctionAST::codegen() {
  auto &P = *Proto;
  FunctionProtos[Proto->getName()] = std::move(Proto);
  Function *TheFunction = getFunction(P.getName());

  if (!TheFunction)
    return nullptr;

  BasicBlock *BB = BasicBlock::Create(*TheContext, "entry", TheFunction);
  Builder->SetInsertPoint(BB);

  NamedValues.clear();
  for (auto &Arg : TheFunction->args())
    NamedValues[std::string(Arg.getName())] = &Arg;

  if (Value *RetVal = Body->codegen()) {
    Builder->CreateRet(RetVal);
    verifyFunction(*TheFunction);

    // Run the optimizer on the function (only in REPL mode, not when building objects)
    if (!BuildObjectMode) {  // <-- Add this check
      TheFPM->run(*TheFunction, *TheFAM);
    }

    return TheFunction;
  }

  TheFunction->eraseFromParent();
  return nullptr;
}
```

Also update `HandleDefinition()` to skip JIT when building objects:

```cpp
static void HandleDefinition() {
  if (auto FnAST = ParseDefinition()) {
    if (CurTok != tok_eol && CurTok != tok_eof) {
      std::string Msg = "Unexpected " + FormatTokenForMessage(CurTok);
      LogError<void>(Msg.c_str());
      SynchronizeToLineBoundary();
      return;
    }
    if (auto *FnIR = FnAST->codegen()) {
      if (ShouldEmitIR) {
        FnIR->print(errs());
        fprintf(stderr, "\n");
      }
      if (!BuildObjectMode) {  // <-- Only JIT in REPL mode
        ExitOnErr(TheJIT->addModule(
            ThreadSafeModule(std::move(TheModule), std::move(TheContext))));
        InitializeModuleAndManagers();
      }
    }
  } else {
    SynchronizeToLineBoundary();
  }
}
```

The key changes:
- When `BuildObjectMode` is true, we skip both per-function optimization and JIT
- The module stays in memory for later optimization and emission

## Rejecting Top-Level Expressions in Build Mode

The REPL lets you type `2 + 2` and see `4.0`. But a `.pyxc` file isn't interactive. Top-level expressions don't make sense.

Update `MainLoop()` to reject them in build mode:

```cpp
static void MainLoop() {
  while (true) {
    if (CurTok == tok_eof)
      return;

    if (CurTok == tok_eol) {
      if (InteractiveMode)
        fprintf(stderr, "ready> ");
      getNextToken();
      continue;
    }

    switch (CurTok) {
    case tok_def:
      HandleDefinition();
      break;
    case tok_extern:
      HandleExtern();
      break;
    default:
      if (BuildObjectMode) {
        LogError<void>("Top-level expressions are not supported in build mode yet");
        SynchronizeToLineBoundary();
      } else {
        HandleTopLevelExpression();
      }
      break;
    }
  }
}
```

Also notice we only print `ready>` when `InteractiveMode` is true.

## Object File Emission

Add a helper to derive output filename from input. Place this before `EmitObjectFile()`:

```cpp
static void EmitTokenStream() {
  fprintf(stderr, "ready> ");
  while (true) {
    const int Tok = gettok();
    if (Tok == tok_eof)
      return;

    fprintf(stderr, "%s", GetTokenName(Tok).c_str());
    if (Tok == tok_eol)
      fprintf(stderr, "\nready> ");
    else
      fprintf(stderr, " ");
  }
}

static std::string DeriveObjectOutputPath(const std::string &InputFile) {  // <-- Add this
  const size_t DotPos = InputFile.find_last_of('.');
  if (DotPos == std::string::npos)
    return InputFile + ".o";
  return InputFile.substr(0, DotPos) + ".o";
}
```

So `fib.pyxc` becomes `fib.o`.

Now the actual object emission helper (optimization is done in `main()` before this is called):

```cpp
static bool EmitObjectFile(const std::string &OutputPath) {
  // Note: Optimization has already been run in main() before calling this function
  InitializeAllAsmParsers();
  InitializeAllAsmPrinters();

  // Get the target triple: a string describing the target architecture
  // Format: <arch>-<vendor>-<os> (e.g., "arm64-apple-darwin23.0.0", "x86_64-unknown-linux-gnu")
  const std::string TargetTriple = sys::getDefaultTargetTriple();

  std::string Error;
  const Target *Target = TargetRegistry::lookupTarget(TargetTriple, Error);
  if (!Target) {
    errs() << Error << "\n";
    return false;
  }

  TargetOptions Opts;
  std::unique_ptr<TargetMachine> TheTargetMachine(Target->createTargetMachine(
      TargetTriple, "generic", "", Opts, Reloc::PIC_));
  if (!TheTargetMachine) {
    errs() << "Could not create target machine.\n";
    return false;
  }

  std::error_code EC;
  raw_fd_ostream Dest(OutputPath, EC, sys::fs::OF_None);
  if (EC) {
    errs() << "Could not open file '" << OutputPath << "': " << EC.message()
           << "\n";
    return false;
  }

  // Use legacy PassManager for code generation (required by TargetMachine)
  legacy::PassManager CodeGenPass;
  if (TheTargetMachine->addPassesToEmitFile(CodeGenPass, Dest, nullptr,
                                            CodeGenFileType::ObjectFile)) {
    errs() << "TheTargetMachine can't emit an object file.\n";
    return false;
  }

  CodeGenPass.run(*TheModule);
  Dest.flush();
  outs() << "Wrote " << OutputPath << "\n";
  return true;
}
```

**Understanding the Target Triple:**

The target triple is a string that identifies the platform we're compiling for. It has the format:

```
<architecture>-<vendor>-<operating-system>
```

Examples:
- `arm64-apple-darwin23.0.0` - Apple Silicon Mac running macOS
- `x86_64-unknown-linux-gnu` - 64-bit Intel/AMD Linux
- `x86_64-pc-windows-msvc` - 64-bit Windows with MSVC

LLVM uses this to determine:
- **Instruction set** - ARM, x86, etc.
- **Calling conventions** - How functions pass arguments
- **Symbol naming** - Whether symbols need prefixes (like `_` on macOS)
- **Object file format** - ELF (Linux), Mach-O (macOS), PE (Windows)

By calling `sys::getDefaultTargetTriple()`, we compile for the current machine. You could also specify a different triple for cross-compilation (e.g., compile on Mac for Linux).

This function only handles code generation. The optimization happens earlier in `main()` (see next section).

You'll need new includes at the top:

```cpp
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/OptimizationLevel.h"  // <-- Add this for O1/O2/O3
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"
```

## Module-Level Optimization

When building with `-O1`, `-O2`, or `-O3`, we run LLVM's full module-level optimization pipeline. This happens in `main()` after parsing completes but before emitting output.

Why module-level instead of per-function?

- **Function inlining** - Can't inline across functions if you optimize each one in isolation
- **Dead code elimination** - A function might be unused but you won't know until you see all callers
- **Interprocedural optimizations** - Many optimizations need to see multiple functions at once

The optimization code in the build command handler (after `MainLoop()` completes):

```cpp
// Initialize all target info (needed for both optimization and code generation)
InitializeAllTargetInfos();
InitializeAllTargets();
InitializeAllTargetMCs();

// Run optimization passes on the module if requested
if (CurrentOptLevel > 0) {
  const std::string TargetTriple = sys::getDefaultTargetTriple();
  TheModule->setTargetTriple(Triple(TargetTriple));

  // ... create TargetMachine for the current platform ...

  // Create the analysis managers
  LoopAnalysisManager LAM;
  FunctionAnalysisManager FAM;
  CGSCCAnalysisManager CGAM;
  ModuleAnalysisManager MAM;

  // Create the PassBuilder and register analyses
  PassBuilder PB(TM.get());
  PB.registerModuleAnalyses(MAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

  // Build the appropriate optimization pipeline
  ModulePassManager MPM;
  if (CurrentOptLevel == 1) {
    MPM = PB.buildPerModuleDefaultPipeline(OptimizationLevel::O1);
  } else if (CurrentOptLevel == 2) {
    MPM = PB.buildPerModuleDefaultPipeline(OptimizationLevel::O2);
  } else {
    MPM = PB.buildPerModuleDefaultPipeline(OptimizationLevel::O3);
  }

  // Run the optimization pipeline
  MPM.run(*TheModule, MAM);
}
```

This uses LLVM's **New PassManager** for optimization, while `EmitObjectFile()` uses the **Legacy PassManager** for code generation. Why the split?

- `TargetMachine::addPassesToEmitFile()` (used for code gen) only supports Legacy PassManager
- LLVM's modern optimization pipelines use New PassManager
- So we optimize with New, then generate code with Legacy

In REPL mode, we still use the per-function optimizer (`TheFPM`) because we're JIT-compiling incrementally. You can't do module-level optimization when you're adding functions one at a time.

## Wiring Up the Build Command

Replace the stub in `main()` with real logic:

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
  const std::string &BuildInputFile = BuildInputFiles.front();
  (void)BuildDebug;

  if (BuildEmit == BuildEmitExe) {
    fprintf(stderr, "build --emit=link: i havent learnt how to do that yet.\n");
    return 1;
  }

  if (!freopen(BuildInputFile.c_str(), "r", stdin)) {
    fprintf(stderr, "Error: could not open file '%s'.\n",
            BuildInputFile.c_str());
    return 1;
  }

  DiagSourceMgr.reset();
  LexLoc = {1, 0};
  CurLoc = {1, 0};
  FunctionProtos.clear();
  HadError = false;
  InteractiveMode = false;
  BuildObjectMode = true;
  CurrentOptLevel = BuildOptLevel;
  ShouldEmitIR = false;

  InitializeModuleAndManagers();
  getNextToken();
  MainLoop();

  if (HadError)
    return 1;

  // Initialize all target info (needed for both optimization and code generation)
  InitializeAllTargetInfos();
  InitializeAllTargets();
  InitializeAllTargetMCs();

  // Run optimization passes on the module if requested
  if (CurrentOptLevel > 0) {
    const std::string TargetTriple = sys::getDefaultTargetTriple();
    TheModule->setTargetTriple(Triple(TargetTriple));

    std::string Error;
    const Target *Target = TargetRegistry::lookupTarget(TargetTriple, Error);
    if (!Target) {
      errs() << Error << "\n";
      return 1;
    }

    TargetOptions Opts;
    std::unique_ptr<TargetMachine> TM(Target->createTargetMachine(
        TargetTriple, "generic", "", Opts, Reloc::PIC_));
    if (!TM) {
      errs() << "Could not create target machine.\n";
      return 1;
    }

    TheModule->setDataLayout(TM->createDataLayout());

    // Create the analysis managers
    LoopAnalysisManager LAM;
    FunctionAnalysisManager FAM;
    CGSCCAnalysisManager CGAM;
    ModuleAnalysisManager MAM;

    // Create the PassBuilder and register analyses
    PassBuilder PB(TM.get());
    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    // Build the appropriate optimization pipeline
    ModulePassManager MPM;
    if (CurrentOptLevel == 1) {
      MPM = PB.buildPerModuleDefaultPipeline(OptimizationLevel::O1);
    } else if (CurrentOptLevel == 2) {
      MPM = PB.buildPerModuleDefaultPipeline(OptimizationLevel::O2);
    } else {
      MPM = PB.buildPerModuleDefaultPipeline(OptimizationLevel::O3);
    }

    // Run the optimization pipeline
    MPM.run(*TheModule, MAM);
  }

  if (BuildEmit == BuildEmitIR) {
    TheModule->print(outs(), nullptr);
    return 0;
  }

  const std::string OutputPath = DeriveObjectOutputPath(BuildInputFile);
  return EmitObjectFile(OutputPath) ? 0 : 1;
}
```

Key steps:

1. Reject `--emit=link` (we'll implement that in a later chapter)
2. Use `freopen()` to redirect stdin to the input file
3. Reset all state (lexer, parser, error flag)
4. Set mode flags: not interactive, building objects
5. Set `CurrentOptLevel` from the `-O` flag
6. Parse the whole file with `MainLoop()`
7. If errors occurred, exit with status 1
8. Initialize target info for optimization and code generation
9. If optimization is requested (`-O1`/`-O2`/`-O3`), run the module-level optimization pipeline using LLVM's New PassManager
10. If `--emit=llvm-ir`, print the (potentially optimized) module to stdout
11. If `--emit=obj`, call `EmitObjectFile()` to generate object code

## REPL Setup

The REPL path also needs to set the mode flags. Update the REPL initialization in `main()`:

```cpp
int main(int argc, const char **argv) {
  cl::ParseCommandLineOptions(argc, argv, "pyxc chapter08\n");

  if (BuildOptLevel > 3) {
    fprintf(stderr, "Error: invalid optimization level -O%u (expected 0..3)\n",
            static_cast<unsigned>(BuildOptLevel));
    return 1;
  }

  if (RunCommand) {
    // ... run command handling ...
  }

  if (BuildCommand) {
    // ... build command handling ...
  }

  DiagSourceMgr.reset();

  if (ReplCommand && ReplEmitTokens) {
    EmitTokenStream();
    return 0;
  }

  // REPL setup starts here
  HadError = false;              // <-- Add this
  InteractiveMode = true;        // <-- Add this
  BuildObjectMode = false;       // <-- Add this
  CurrentOptLevel = 0;           // <-- Add this
  if (InteractiveMode)
    fprintf(stderr, "ready> ");
  getNextToken();

  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();
  InitializeNativeTargetAsmParser();
  TheJIT = ExitOnErr(PyxcJIT::Create());

  // Make the module, which holds all the code and optimization managers.
  InitializeModuleAndManagers();
  ShouldEmitIR = ReplEmitIR;

  // Run the main "interpreter loop" now.
  MainLoop();

  return 0;
}
```

This ensures REPL always uses `-O0` behavior and enables prompts.

## Update Main Description

Change the description string so errors show the right chapter:

```cpp
int main(int argc, const char **argv) {
  cl::ParseCommandLineOptions(argc, argv, "pyxc chapter08\n");  // <-- Update this

  if (BuildOptLevel > 3) {
    fprintf(stderr, "Error: invalid optimization level -O%u (expected 0..3)\n",
            static_cast<unsigned>(BuildOptLevel));
    return 1;
  }
  // ...
}
```

## Compile and Test

```bash
cd code/chapter08
./build.sh
```

Or manually:
```bash
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
```

## Sample Session: Emit LLVM IR

Create `fib.pyxc`:
```python
def fib(n): return n
```

Emit LLVM IR:
```bash
$ ./build/pyxc build fib.pyxc --emit=llvm-ir
define double @fib(double %n) {
entry:
  ret double %n
}
```

## Sample Session: Emit Object File

```bash
$ ./build/pyxc build fib.pyxc --emit=obj
Wrote fib.o
$ file fib.o
fib.o: Mach-O 64-bit object arm64
```

You can inspect it with `nm`:
```bash
$ nm fib.o
0000000000000000 T _fib
```

## Sample Session: Optimization Levels

Create `add.pyxc`:
```python
def add(x, y): return x + y + y
```

Build without optimization:
```bash
$ ./build/pyxc build add.pyxc --emit=llvm-ir -O0
define double @add(double %x, double %y) {
entry:
  %addtmp = fadd double %x, %y
  %addtmp1 = fadd double %addtmp, %y
  ret double %addtmp1
}
```

Build with `-O3`:
```bash
$ ./build/pyxc build add.pyxc --emit=llvm-ir -O3
define double @add(double %x, double %y) {
entry:
  %0 = fadd double %y, %y
  %addtmp1 = fadd double %0, %x
  ret double %addtmp1
}
```

Notice `-O3` reassociated the adds to group `y + y` first. This can enable further optimizations if the compiler knows both uses of `y` are the same value.

## What We Built

- **File-based compilation** - Read `.pyxc` from disk, not just stdin
- **Object file emission** - Generate `.o` files you can link with other code
- **LLVM IR emission** - Print the full module's IR to stdout
- **Proper optimization pipeline** - Use LLVM's full module optimization pipeline for `-O1`/`-O2`/`-O3`:
  - Skip per-function optimization during codegen when building objects
  - Run complete module-level optimization before code generation
  - Use `PassBuilder::buildPerModuleDefaultPipeline()` for proper optimization including inlining, dead code elimination, and interprocedural analysis
- **Error tracking** - Exit with status 1 when compilation fails
- **Mode separation** - REPL behavior vs. batch build behavior

The `build` subcommand is now real. You can compile Pyxc source to object files with proper LLVM optimization.

### Two-Stage Compilation Architecture

We now use the proper two-stage approach that real compilers use:

**REPL Mode:**
- Per-function optimization with `TheFPM` (for fast incremental compilation)
- JIT execution immediately

**Build Mode:**
- Skip per-function optimization during parsing
- Accumulate all functions in a module
- Run full module optimization pipeline (includes function, module, and interprocedural passes)
- Generate object code

This is exactly how Clang and other LLVM-based compilers work.

## Using Pyxc Object Files from C++

Now that we can generate object files, let's demonstrate interoperability with C++. We'll compile Pyxc code to an object file and call it from a C++ program.

Create `math.pyxc` with some simple math functions:

```python
def square(x):
  return x * x

def add(x, y):
  return x + y

def mul(x, y):
  return x * y

def distance(x1, y1, x2, y2):
  return square(x2 - x1) + square(y2 - y1)
```

Compile it to an object file:

```bash
$ ./build/pyxc build math.pyxc --emit=obj -O2
Wrote math.o
```

Create a C++ program `use_math.cpp` that calls these functions:

```cpp
#include <cstdio>
#include <cmath>

// Declare functions compiled from Pyxc
extern "C" {
  double square(double x);
  double add(double x, double y);
  double mul(double x, double y);
  double distance(double x1, double y1, double x2, double y2);
}

int main() {
  printf("Calling Pyxc math functions from C++:\n\n");

  double x = 5.0;
  printf("square(%.1f) = %.1f\n", x, square(x));

  printf("add(3.0, 4.0) = %.1f\n", add(3.0, 4.0));
  printf("mul(6.0, 7.0) = %.1f\n", mul(6.0, 7.0));

  double x1 = 0.0, y1 = 0.0;
  double x2 = 3.0, y2 = 4.0;
  double dist = distance(x1, y1, x2, y2);
  printf("distance((%.1f,%.1f), (%.1f,%.1f)) = %.1f\n", x1, y1, x2, y2, dist);
  printf("  (sqrt of that is %.2f)\n", sqrt(dist));

  return 0;
}
```

**Key points:**

- `extern "C"` tells C++ to use C linkage (simple names like `square` instead of decorated names with type information)
- Our Pyxc functions return `double` and take `double` parameters
- We declare the prototypes but don't define them - they come from `math.o`

Link and run:

```bash
$ clang++ -o math_demo use_math.cpp math.o
$ ./math_demo
Calling Pyxc math functions from C++:

square(5.0) = 25.0
add(3.0, 4.0) = 7.0
mul(6.0, 7.0) = 42.0
distance((0.0,0.0), (3.0,4.0)) = 25.0
  (sqrt of that is 5.00)
```

It works! The C++ code seamlessly calls functions compiled from Pyxc.

### Optimization in Action

Let's look at the optimized IR to see what `-O2` did:

```bash
$ ./build/pyxc build math.pyxc --emit=llvm-ir -O3
```

The `distance` function after optimization:

```llvm
define double @distance(double %x1, double %y1, double %x2, double %y2) {
entry:
  %subtmp = fsub double %x2, %x1
  %multmp.i = fmul double %subtmp, %subtmp
  %subtmp1 = fsub double %y2, %y1
  %multmp.i3 = fmul double %subtmp1, %subtmp1
  %addtmp = fadd double %multmp.i, %multmp.i3
  ret double %addtmp
}
```

Notice the calls to `square` were **inlined**! The optimizer saw:
- `distance` calls `square` twice
- `square` is just `x * x`
- So it replaced the calls with the actual multiplications

This is interprocedural optimization at work - the module-level optimization pipeline we set up can see across function boundaries and make these optimizations.

Check the generated ARM64 assembly:

```bash
$ objdump -d math.o
```

The optimized machine code for `distance`:

```nasm
fsub d1, d3, d1    ; y2 - y1
fsub d0, d2, d0    ; x2 - x1
fmul d1, d1, d1    ; (y2-y1)²
fmul d0, d0, d0    ; (x2-x1)²
fadd d0, d0, d1    ; (x2-x1)² + (y2-y1)²
ret
```

Six instructions total (4 arithmetic + return). The inlining eliminated all function call overhead!

## Testing Your Implementation

This chapter includes **47 automated tests**. Run them with:

```bash
cd code/chapter08/test
lit -v .
# or: llvm-lit -v .
```

**Pro tip:** The test directory shows exactly what the language can do! Key tests include:
- `cli_build_emit_obj.pyxc` - Verifies object file creation
- `cli_build_opt_levels.pyxc` - Verifies `-O0` vs `-O3` produces different IR
- `opt_inline_*.pyxc` - Shows function inlining optimizations
- `cpp_interop_*.pyxc` - Tests for C++ interoperability

Browse the tests to understand what's possible at this stage!

## What's Next

**Chapter 9** adds debug information (`-g` flag) so we can debug Pyxc code with `lldb` and see source code in the debugger.

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
