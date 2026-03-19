---
description: "Add ORC JIT and an optimization pass pipeline: top-level expressions now execute immediately and functions come out smaller."
---
# 6. Pyxc: JIT and Optimization

## Where We Are

[Chapter 5](chapter-05.md) produces correct IR, but you have to read it — nothing runs. Define `foo` and call it with `2`, and you get IR back with no result:

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

## New Globals

Chapter 5 had three globals: `TheContext`, `TheModule`, and `Builder`. Chapter 6 adds several more:

```cpp
static unique_ptr<PyxcJIT>              TheJIT;       // the ORC JIT instance
static unique_ptr<FunctionPassManager>  TheFPM;       // runs optimizations per function
static unique_ptr<LoopAnalysisManager>  TheLAM;       // analysis cache: per loop
static unique_ptr<FunctionAnalysisManager> TheFAM;   // analysis cache: per function
static unique_ptr<CGSCCAnalysisManager> TheCGAM;      // analysis cache: per call graph
static unique_ptr<ModuleAnalysisManager> TheMAM;      // analysis cache: per module
static unique_ptr<PassInstrumentationCallbacks> ThePIC; // pass debug hook registry
static unique_ptr<StandardInstrumentations>     TheSI;  // built-in timing/print hooks
static map<string, unique_ptr<PrototypeAST>> FunctionProtos; // persistent prototype registry
static ExitOnError ExitOnErr;                         // terminates on unrecoverable JIT error
```

`TheJIT` is created once in `main()` and lives for the whole session. Everything else is recreated each time `InitializeModuleAndManagers()` is called — which happens at startup and after each module is handed to the JIT.

## What Is ORC JIT?

ORC stands for **On-Request Compilation**. It is LLVM's current JIT framework — a library for building JIT compilers, not a single fixed JIT.

The key idea is lazy compilation: ORC doesn't compile everything upfront. It compiles a function when something first asks for its address. You add a module to the JIT and get back a handle; the native code is produced on demand when a symbol is first looked up.

For our purposes we use `PyxcJIT` (see `include/PyxcJIT.h`), a thin wrapper around ORC's `LLJIT`. It is created once in `main()`:

```cpp
InitializeNativeTarget();
InitializeNativeTargetAsmPrinter();
InitializeNativeTargetAsmParser();
TheJIT = ExitOnErr(PyxcJIT::Create());
InitializeModuleAndManagers();
```

`InitializeNativeTarget*` registers the host machine's backend with LLVM — without it, LLVM doesn't know how to generate code for your CPU.

`PyxcJIT::Create()` initializes the **native target** (the CPU and OS the compiler is running on) and sets up the **dynamic linker** (so `extern` functions like `sin` resolve to the real C library at call time).

The dynamic linker part is worth pausing on. `pyxc` is a C++ program linked against the C standard library, so `sin`, `cos`, `printf`, and every other standard function are already loaded into the `pyxc` process when it starts. When the JIT can't resolve a symbol internally, ORC searches `pyxc`'s own symbol table — and finds them there. No `#include`, no link flags, no explicit registration. We'll see this in "Try It" below.

## One Module Per Compilation Unit

In chapter 5, one module accumulated everything typed in the session. Chapter 6 gives each top-level input its own module. Here's why.

When you type `foo(2)`, Pyxc wraps it in a zero-argument function called `__anon_expr`, compiles it, runs it, and then wants to delete the compiled native code — you don't want `__anon_expr` accumulating in the JIT forever.

The JIT frees native code via `ResourceTracker::remove()`. A `ResourceTracker` scopes everything compiled from a single `addModule` call. `RT->remove()` frees all of that at once — but it operates at module granularity, not function granularity.

That's the constraint. If `__anon_expr` and `foo` were compiled from the same module, `RT->remove()` would free `foo`'s native code too. Calling `foo` afterwards would be a crash.

The solution: give each top-level input its own module. `def foo` gets compiled into module A, handed to the JIT, and stays there permanently. `foo(2)` gets compiled into module B, attached to a `ResourceTracker`, and freed immediately after execution. Module A is untouched.

This is why `InitializeModuleAndManagers()` is called both at startup and after every module transfer:

```cpp
// Hand the module to the JIT.
ExitOnErr(TheJIT->addModule(ThreadSafeModule(move(TheModule), move(TheContext))));

// Start fresh for the next input.
InitializeModuleAndManagers();
```

`ThreadSafeModule` packages the module and its context together for safe handoff to the JIT's internal threads.

## InitializeModuleAndManagers

`InitializeModuleAndManagers()` replaces `InitializeModule()` from chapter 5. It creates a fresh module and wires up the optimization pipeline:

```cpp
static void InitializeModuleAndManagers() {
  // Fresh context and module for this compilation unit.
  TheContext = make_unique<LLVMContext>();
  TheModule  = make_unique<Module>("PyxcJIT", *TheContext);
  // Tell the module how the JIT lays out data for the host machine,
  // so codegen emits correctly-sized types and pointer widths.
  TheModule->setDataLayout(TheJIT->getDataLayout());
  Builder = make_unique<IRBuilder<>>(*TheContext);

  // Create the pass and analysis managers.
  TheFPM  = make_unique<FunctionPassManager>();
  TheLAM  = make_unique<LoopAnalysisManager>();
  TheFAM  = make_unique<FunctionAnalysisManager>();
  TheCGAM = make_unique<CGSCCAnalysisManager>();
  TheMAM  = make_unique<ModuleAnalysisManager>();
  ThePIC  = make_unique<PassInstrumentationCallbacks>();
  TheSI   = make_unique<StandardInstrumentations>(*TheContext,
                                                  /*DebugLogging*/ false);
  TheSI->registerCallbacks(*ThePIC, TheMAM.get());

  // Add optimization passes (skipped entirely at -O0).
  if (OptLevel != 0) {
    TheFPM->addPass(InstCombinePass()); // peephole rewrites: x+0→x, x*1→x
    TheFPM->addPass(ReassociatePass()); // reorder ops: (x+2)+3 → x+(2+3) → x+5
    TheFPM->addPass(GVNPass());         // eliminate redundant computations
    TheFPM->addPass(SimplifyCFGPass()); // remove dead branches and blocks
  }

  // Cross-register so passes can request any analysis tier they need.
  PassBuilder PB;
  PB.registerModuleAnalyses(*TheMAM);
  PB.registerFunctionAnalyses(*TheFAM);
  PB.crossRegisterProxies(*TheLAM, *TheFAM, *TheCGAM, *TheMAM);
}
```

The pipeline runs immediately after each function is codegenned and verified:

```cpp
// In FunctionAST::codegen, after verifyFunction:
TheFPM->run(*TheFunction, *TheFAM);
```

## The Optimization Pipeline

LLVM ships dozens of optimization passes. They are not enabled by default — there is no "turn on all optimizations" switch. You pick the passes that make sense for your language. Four run on each function, in this order:

| Pass | What it does |
|---|---|
| `InstCombinePass` | Simplifies individual instructions: `x * 1 → x`, `x + 0 → x`, and similar peephole rewrites |
| `ReassociatePass` | Reorders additions and multiplications so constants end up together: `(x+2)+3` becomes `x+(2+3)`, which then collapses to `x+5` |
| `GVNPass` | Finds places where the same value is computed twice and removes the duplicate. This is what eliminates the second `fadd` in `foo` |
| `SimplifyCFGPass` | Removes dead branches and unreachable blocks — not relevant yet, but essential once we add `if`/`while` |

Note that `1+2` collapsing to `3` isn't done by any of these passes — `IRBuilder` does it automatically as it constructs the IR. That's why `(1+2+x)` already shows `3.000000e+00` in the output before any pass runs.

### Analysis Managers

The four analysis managers (`TheFAM`, `TheLAM`, `TheCGAM`, `TheMAM`) are a shared cache that passes read from rather than recomputing things themselves. For our four passes, `TheFAM` (per-function) does the real work — the other three are registered because the pass manager requires all four tiers to be present. If a pass requests an analysis and the manager isn't there, LLVM asserts and crashes.

`ThePIC` and `TheSI` are how you ask LLVM to print the IR before and after each pass. Change `/*DebugLogging*/ false` to `true` in `InitializeModuleAndManagers` and every pass will dump the IR as it runs — useful when you want to see exactly which pass is responsible for a transformation.

## getFunction and the Cross-Module Problem

In chapter 5, `CallExprAST::codegen` called `TheModule->getFunction(Callee)` directly. That breaks with per-module lifetime: if `foo` was compiled and its module handed to the JIT, the current module has no record of `foo`. A call to `foo(2)` would fail with "Unknown function referenced."

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

**`RT->remove()`** frees the object file, symbol table entries, and executable memory for `__anon_expr`. This replaces the `eraseFromParent()` call from chapter 5 — instead of removing the IR before JIT, we compile it first and then free the resulting native code.

Named functions (`def foo`) are added to the JIT's main dylib without a tracker — they stay compiled permanently.

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
