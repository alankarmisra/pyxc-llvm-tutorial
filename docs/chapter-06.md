---
description: "Add ORC JIT and an optimization pass pipeline: top-level expressions now execute immediately and functions come out smaller."
---
# 6. Pyxc: JIT and Optimization

## Where We Are

[Chapter 5](chapter-05.md) produces correct IR, but you have to read it — nothing runs. For example:

<!-- code-merge:start -->
```python
ready> def foo(x): return (1+2+x)*(x+(1+2))
Parsed a function definition.
```

```llvm
define double @foo(double %x) {
entry:
  %addtmp = fadd double 3.000000e+00, %x
  %addtmp1 = fadd double %x, 3.000000e+00
  %multmp = fmul double %addtmp, %addtmp1
  ret double %multmp
}
```

```python
ready> foo(2)
Parsed a top-level expression.
```

```llvm
define double @__anon_expr() {
entry:
  %calltmp = call double @foo(double 2.000000e+00)
  ret double %calltmp
}
```
<!-- code-merge:end -->

Two problems. First, `foo(2)` doesn't evaluate — you see the IR for the call but no result. Second, the IR for `foo` isn't as clean as it could be: `(1+2+x)*(x+(1+2))` produces two separate `fadd` instructions even though both sides of the multiply are the same expression `x+3`.

By the end of this chapter, calling `foo(2)` prints the answer:

<!-- code-merge:start -->
```python
ready> foo(2)
Parsed a top-level expression.
```

```llvm
define double @__anon_expr() {
entry:
  %calltmp = call double @foo(double 2.000000e+00)
  ret double %calltmp
}
```

```bash
Evaluated to 25.000000
```
<!-- code-merge:end -->

And `foo` itself comes out of the optimizer with the redundant computation eliminated:

<!-- code-merge:start -->
```python
ready> def foo(x): return (1+2+x)*(x+(1+2))
Parsed a function definition.
```

```llvm
define double @foo(double %x) {
entry:
  %addtmp = fadd double %x, 3.000000e+00
  %multmp = fmul double %addtmp, %addtmp
  ret double %multmp
}
```
<!-- code-merge:end -->

Two instructions instead of three. One `fadd` instead of two — the optimizer recognized that both factors are `x+3` and computed it once.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-06
```

## Optimizer and Analysis Managers

[Chapter 5](chapter-05.md) had three globals: `TheContext`, `TheModule`, and `Builder`. Chapter 6 adds several more:

```cpp
static unique_ptr<PyxcJIT>              TheJIT;       // the ORC JIT instance
static unique_ptr<FunctionPassManager>  TheFPM;       // runs optimizations per function
static unique_ptr<LoopAnalysisManager>     TheLAM;    // stores results of loop analyses
static unique_ptr<FunctionAnalysisManager> TheFAM;    // stores results of function analyses
static unique_ptr<CGSCCAnalysisManager>    TheCGAM;   // stores results of call-graph analyses
static unique_ptr<ModuleAnalysisManager>   TheMAM;    // stores results of module analyses
static unique_ptr<PassInstrumentationCallbacks> ThePIC; // pass debug hook registry
static unique_ptr<StandardInstrumentations>     TheSI;  // built-in timing/print hooks
static map<string, unique_ptr<PrototypeAST>> FunctionProtos; // persistent prototype registry
static ExitOnError ExitOnErr;                         // terminates on unrecoverable JIT error
```

## What Is ORC JIT?

ORC stands for **On-Request Compilation**. It is LLVM's current JIT framework — a library for building JIT compilers, not a single fixed JIT.

ORC uses lazy compilation: it doesn't compile everything upfront. It compiles a function when something first asks for its address. You add a module to the JIT and get back a handle; actual compilation happens later, on demand.

For our purposes we use `PyxcJIT` (see [include/PyxcJIT.h](https://github.com/alankarmisra/pyxc-llvm-tutorial/blob/main/code/include/PyxcJIT.h)), a thin wrapper around ORC's `LLJIT`. It is created once in `main()`:

```cpp
InitializeNativeTarget();
InitializeNativeTargetAsmPrinter();
InitializeNativeTargetAsmParser();
TheJIT = ExitOnErr(PyxcJIT::Create());
InitializeModuleAndManagers();
```

`InitializeNativeTarget*` registers the host machine's backend with LLVM — without it, LLVM doesn't know how to generate code for your CPU.

`PyxcJIT::Create()` initializes the **native target** (the CPU and OS the compiler is running on) and sets up the **dynamic linker** (so `extern` functions like `sin` resolve to the real C library at call time).

The dynamic linker part is worth pausing on. `pyxc` is a C++ program linked against the C standard library, so `sin`, `cos`, `printf`, and every other standard function are already loaded into the `pyxc` process when it starts. When the JIT can't resolve a name internally, ORC searches the functions already loaded into the `pyxc` process — and finds them there. No `#include`, no link flags, no explicit registration. We'll see this in "Try It" below.

## InitializeModuleAndManagers

`InitializeModuleAndManagers()` replaces `InitializeModule()` from [chapter 5](chapter-05.md). It is called at startup and after every module is handed to the JIT. It does four things.

**1. Create a fresh module**

```cpp
TheContext = make_unique<LLVMContext>();
TheModule  = make_unique<Module>("PyxcJIT", *TheContext);
TheModule->setDataLayout(TheJIT->getDataLayout());
Builder = make_unique<IRBuilder<>>(*TheContext);
```

Same as chapter 5's `InitializeModule()`, with one addition: `setDataLayout` tells the module how the JIT lays out data for the host machine. This is an implementation detail you don't need to think about for now — it would only matter if you were targeting a different platform than the one you're compiling on.

**2. Create the pass and analysis managers**

```cpp
TheFPM  = make_unique<FunctionPassManager>();
TheLAM  = make_unique<LoopAnalysisManager>();
TheFAM  = make_unique<FunctionAnalysisManager>();
TheCGAM = make_unique<CGSCCAnalysisManager>();
TheMAM  = make_unique<ModuleAnalysisManager>();
ThePIC  = make_unique<PassInstrumentationCallbacks>();
TheSI   = make_unique<StandardInstrumentations>(*TheContext, /*DebugLogging*/ false);
TheSI->registerCallbacks(*ThePIC, TheMAM.get());
```

`TheFPM` is the pass manager — it runs optimization passes on each function in sequence.

The last two lines wire up LLVM's built-in pass observer. `ThePIC` is a registry of callbacks — functions to call before and after each pass runs. `TheSI` is LLVM's built-in collection of those callbacks (IR printing, timing, statistics), connected to `TheMAM` because printing IR can require module-level analysis. One useful thing this enables: change `/*DebugLogging*/ false` to `true` and every pass will dump the IR before and after it runs — helpful when you want to see exactly which pass changed the IR and how.

**3. Register optimization passes**

```cpp
if (OptLevel != 0) {
  TheFPM->addPass(InstCombinePass());
  TheFPM->addPass(ReassociatePass());
  TheFPM->addPass(GVNPass());
  TheFPM->addPass(SimplifyCFGPass());
}
```

`OptLevel` is a command-line flag covered in [Command-Line Parsing](#command-line-parsing) below — for now just read this as "skip optimizations if the user asked for none." The passes themselves are explained in [The Optimization Pipeline](#the-optimization-pipeline).

**4. Wire the managers together**

```cpp
PassBuilder PB;
PB.registerModuleAnalyses(*TheMAM);
PB.registerFunctionAnalyses(*TheFAM);
PB.crossRegisterProxies(*TheLAM, *TheFAM, *TheCGAM, *TheMAM);
```

`crossRegisterProxies` links all four analysis managers together so that a pass running at one level (say, function) can request results from another level (say, module) if it needs them. All four must be present or the pass manager will crash.

The pipeline runs immediately after each function is codegenned and verified:

```cpp
// In FunctionAST::codegen, after verifyFunction:
TheFPM->run(*TheFunction, *TheFAM);
```

## The Optimization Pipeline

LLVM ships dozens of optimization passes and `PassBuilder` has predefined pipeline presets (`O1`, `O2`, `O3`) that enable many of them at once. We're not using those presets — we manually add four specific passes instead, so it's easy to see exactly what's running. A natural next step would be to wire Pyxc's `-O` flag directly into one of those presets rather than maintaining a hand-picked list. For now the four passes are:

| Pass | What it does |
|---|---|
| `InstCombinePass` | Simplifies individual instructions: `x * 1 → x`, `x + 0 → x`, and similar peephole rewrites |
| `ReassociatePass` | Reorders additions and multiplications so constants end up together: `(x+2)+3` becomes `x+(2+3)`, which then collapses to `x+5` |
| `GVNPass` | Finds places where the same value is computed twice and removes the duplicate. This is what eliminates the second `fadd` in `foo` |
| `SimplifyCFGPass` | Removes dead branches and unreachable blocks — not relevant yet, but essential once we add `if`/`while` |

Note that `1+2` collapsing to `3` isn't done by any of these passes — `IRBuilder` does it automatically as it constructs the IR. That's why `(1+2+x)` already shows `3.000000e+00` in the output before any pass runs.

## Executing Top-Level Expressions

`HandleTopLevelExpression` codegens and optimizes the anonymous function exactly as before, then executes it:

```cpp
if (auto *FnIR = FnAST->codegen()) {
  FnIR->print(errs());

  // Scope this expression's compiled code to a ResourceTracker so we can
  // free it precisely after execution, without disturbing other symbols.
  auto RT = TheJIT->getMainJITDylib().createResourceTracker();

  // Transfer the module to the JIT. TheModule is now owned by the JIT.
  auto TSM = ThreadSafeModule(move(TheModule), move(TheContext));
  ExitOnErr(TheJIT->addModule(move(TSM), RT));

  // Create a fresh module for the next input.
  InitializeModuleAndManagers();

  // Look up the compiled function by name and cast its address to a
  // callable function pointer.
  auto ExprSymbol = ExitOnErr(TheJIT->lookup("__anon_expr"));
  double (*FP)() = ExprSymbol.toPtr<double (*)()>();

  // Execute the compiled native code directly on the host CPU.
  fprintf(stderr, "Evaluated to %f\n", FP());

  // Release the compiled code and JIT memory for this expression.
  ExitOnErr(RT->remove());
}
```

**`ExprSymbol.toPtr<double(*)()>()`** gets the native machine-code address of the compiled `__anon_expr` function and casts it to a C function pointer. `FP()` runs the compiled code directly on the CPU — no interpreter, no virtual machine.

**`RT->remove()`** frees the object file, symbol table entries, and executable memory for `__anon_expr`. This replaces the `eraseFromParent()` call from [chapter 5](chapter-05.md) — instead of removing the IR before JIT, we compile it first and then free the resulting native code.

Named functions (`def foo`) are added to the JIT's main dylib without a tracker — they stay compiled permanently.

## One Module Per Compilation Unit

When you type `foo(2)`, Pyxc wraps it in a zero-argument function called `__anon_expr`, compiles it, runs it, and then frees it — anonymous expressions shouldn't accumulate in the JIT forever. The JIT frees native code at module granularity via `ResourceTracker::remove()`, so `__anon_expr` needs its own module or removing it would take `foo` with it.

Chapter 6 uses a simple strategy: every top-level input gets its own fresh module. A smarter approach would give anonymous expressions their own modules and batch named functions together — but the simple version is easier to follow, and we'll revisit it later.

One side effect worth noting: since each `def` lands in its own module, you might expect to be able to redefine a function by typing `def foo` a second time. LLVM won't allow it — redefining a function is an error.

This is why `InitializeModuleAndManagers()` is called both at startup and after every module transfer:

```cpp
// Hand the module to the JIT.
ExitOnErr(TheJIT->addModule(ThreadSafeModule(move(TheModule), move(TheContext))));

// Start fresh for the next input.
InitializeModuleAndManagers();
```

`ThreadSafeModule` packages the module and its context together for safe handoff to the JIT's internal threads.

## getFunction and the Cross-Module Problem

In [chapter 5](chapter-05.md), `CallExprAST::codegen` called `TheModule->getFunction(Callee)` directly. That breaks with per-module lifetime: if `foo` was compiled and its module handed to the JIT, the current module has no record of `foo`. A call to `foo(2)` would fail with "Unknown function referenced."

The solution is a persistent prototype registry, `FunctionProtos`, and a helper that uses it:

```cpp
Function *getFunction(string Name) {
  // Fast path: already declared or defined in the current module.
  if (auto *F = TheModule->getFunction(Name))
    return F;

  // Slow path: re-emit a declaration from the saved prototype.
  auto FI = FunctionProtos.find(Name);
  if (FI != FunctionProtos.end())
    return FI->second->codegen(); // emits a fresh 'declare' in the current module

  return nullptr;
}
```

When `foo` is compiled, its `PrototypeAST` is saved into `FunctionProtos`. When a new module is created and `foo(2)` is codegenned, `getFunction` doesn't find `foo` in the fresh module — so it calls `codegen()` on the saved prototype, emitting a `declare double @foo(double)` in the current module. The JIT then resolves that `declare` to the already-compiled body.

`extern def` declarations follow the same pattern. After codegen, the prototype is saved:

```cpp
// In HandleExtern — save the prototype so getFunction() can re-emit it later.
FunctionProtos[ProtoAST->getName()] = move(ProtoAST);
```

And every function definition registers itself before codegenning the body:

```cpp
// In FunctionAST::codegen — register prototype before resolving the Function*.
auto &P = *Proto;
FunctionProtos[Proto->getName()] = move(Proto);
Function *TheFunction = getFunction(P.getName());
```

## The Runtime Library

Chapter 6 adds two built-in functions callable from Pyxc via `extern def`:

```cpp
extern "C" DLLEXPORT double putchard(double X) {
  fputc((char)X, stderr); // print a single ASCII character
  return 0;
}

extern "C" DLLEXPORT double printd(double X) {
  fprintf(stderr, "%f\n", X); // print a double
  return 0;
}
```

They are compiled into the `pyxc` binary with C linkage. Because ORC's dynamic linker searches the host process's symbol table, `extern def printd(x)` resolves to this function automatically at runtime — no registration required.

`DLLEXPORT` is a no-op on macOS and Linux. On Windows, symbols are not exported from executables by default, so the macro expands to `__declspec(dllexport)` to make them visible to the JIT's linker.

## Command-Line Parsing

Chapter 6 adds a `-O` flag to control the optimization level. Rather than parsing `argv` by hand, we use LLVM's `CommandLine` library:

```cpp
static cl::OptionCategory PyxcCategory("Pyxc options");

static cl::opt<unsigned> OptLevel(
    "O",                          // flag name: -O
    cl::desc("Optimization level"),
    cl::value_desc("0|1|2|3"),
    cl::Prefix,                   // allows -O2 instead of -O=2
    cl::init(2),                  // default: -O2
    cl::cat(PyxcCategory));
```

`cl::Prefix` lets users write `-O2` rather than `-O=2`. LLVM generates `--help` output automatically from the `cl::desc` strings.

With `-O0`, `InitializeModuleAndManagers` skips `addPass` entirely and the pipeline is empty — functions come out exactly as the IR builder constructed them, with no transformations. This is useful when you want to see the unoptimized IR while debugging a new language feature.

## Build and Run

```bash
cd code/chapter-06
cmake -S . -B build && cmake --build build
./build/pyxc
```

## Try It

### extern resolves from the process

<!-- code-merge:start -->
```python
ready> extern def sin(x)
```

```bash
Parsed an extern.
```

```llvm
declare double @sin(double)
```

```python
ready> sin(1)
```

```bash
Parsed a top-level expression.
```

```llvm
define double @__anon_expr() {
entry:
  %calltmp = call double @sin(double 1.000000e+00)
  ret double 0x3FEAED548F090CEE
}
```

```bash
Evaluated to 0.841471
```
<!-- code-merge:end -->

Notice what just happened. We declared `sin` as an extern and immediately called it — and it worked. We didn't link against anything. We didn't pass any flags. We didn't register `sin` anywhere.

Here's why. The `pyxc` binary is a C++ program linked against the C standard library. That library — which contains `sin`, `cos`, `sqrt`, `printf`, and hundreds of other functions — is loaded into the process when `pyxc` starts. All of its symbols are visible in the process's symbol table. When the JIT looks up `sin`, it searches that same symbol table and finds the address of the real `sin` that's already loaded.

This is true of any function in any shared library already loaded into the process. On macOS and Linux, the C standard library and the system math library are always loaded. Every C library function is available to Pyxc programs this way, for free. The hex value `0x3FEAED548F090CEE` is the IEEE 754 encoding of `sin(1) ≈ 0.841471`.

### The Pythagorean identity

<!-- code-merge:start -->
```python
ready> extern def cos(x)
```

```bash
Parsed an extern.
```

```llvm
declare double @cos(double)
```

```python
ready> def foo(x): return sin(x)*sin(x)+cos(x)*cos(x)
```

```bash
Parsed a function definition.
```

```llvm
define double @foo(double %x) {
entry:
  %calltmp  = call double @sin(double %x)
  %calltmp1 = call double @sin(double %x)
  %multmp   = fmul double %calltmp, %calltmp1
  %calltmp2 = call double @cos(double %x)
  %calltmp3 = call double @cos(double %x)
  %multmp4  = fmul double %calltmp2, %calltmp3
  %addtmp   = fadd double %multmp, %multmp4
  ret double %addtmp
}
```

```python
ready> foo(4)
```

```bash
Evaluated to 1.000000
```
<!-- code-merge:end -->

`sin²(x) + cos²(x) = 1` for any x — the Pythagorean identity. The JIT compiled `foo`, resolved the native `sin` and `cos`, and executed the whole thing. The call duplication (two calls to `sin`, two to `cos`) is a known limitation covered below.

### The optimizer at work

<!-- code-merge:start -->
```python
ready> def foo(x): return (1+2+x)*(x+(1+2))
```

```bash
Parsed a function definition.
```

```llvm
define double @foo(double %x) {
entry:
  %addtmp = fadd double %x, 3.000000e+00
  %multmp = fmul double %addtmp, %addtmp
  ret double %multmp
}
```

```python
ready> foo(2)
```

```bash
Evaluated to 25.000000
```
<!-- code-merge:end -->

Six source operations, two IR instructions. `IRBuilder` folded `1+2` to `3.0` at construction time. `ReassociatePass` and `GVNPass` recognized that both factors of the multiply are `x+3` and eliminated the duplicate `fadd`, leaving `%addtmp * %addtmp`. `(3+2)*(2+3) = 5*5 = 25`. Correct.

### The runtime library

<!-- code-merge:start -->
```python
ready> extern def printd(x)
```

```bash
Parsed an extern.
```

```llvm
declare double @printd(double)
```

```python
ready> printd(42)
```

```bash
42.000000
Evaluated to 0.000000
```
<!-- code-merge:end -->

`42.000000` is printed by `printd`'s own `fprintf`. `Evaluated to 0.000000` is the JIT printing `printd`'s return value (always `0.0`) after executing the `__anon_expr` wrapper.

## Known Limitations

- **Duplicate extern calls not eliminated.** `sin(x)*sin(x)` calls `sin` twice. GVN cannot merge calls to extern functions without alias information marking them as pure (no side effects). LLVM function attributes can express this; a later chapter can add them.

## What's Next

[Chapter 7](chapter-07.md) adds file input mode and a `-v` flag for IR inspection — `pyxc script.pyxc` runs a source file through the same JIT pipeline as the REPL, and `pyxc script.pyxc -v` prints the generated IR. That's the foundation real programs need.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.
