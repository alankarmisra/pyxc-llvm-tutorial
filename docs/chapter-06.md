---
description: "Add ORC JIT and an optimisation pass pipeline: top-level expressions now execute immediately and functions come out smaller."
---
# 6. Pyxc: JIT and Optimisation

## Where We Are

Chapter 5 produces correct IR, but you have to read it — nothing runs:

```
ready> 4 + 5
Parsed a top-level expression.
define double @__anon_expr() {
entry:
  ret double 9.000000e+00
}
```

And the IR for non-trivial functions has no unnecessary instructions but also no optimisation. `sin(x)*sin(x) + cos(x)*cos(x)` emits four `call` instructions and two `fmul`s even though there's a simpler form.

By the end of this chapter:

```
ready> 4 + 5
Parsed a top-level expression.
define double @__anon_expr() {
entry:
  ret double 9.000000e+00
}
Evaluated to 9.000000
```

And a function that looks like `(1+2+x)*(x+(1+2))` comes out of the optimiser as:

```llvm
define double @test(double %x) {
entry:
  %addtmp = fadd double %x, 3.000000e+00
  %multmp = fmul double %addtmp, %addtmp
  ret double %addtmp
}
```

Two instructions instead of six. The constants folded, the repeated sub-expressions merged.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-06
```

## New Globals

Chapter 5 had four globals. Chapter 6 adds seven more:

```cpp
static unique_ptr<PyxcJIT>                      TheJIT;
static unique_ptr<FunctionPassManager>          TheFPM;
static unique_ptr<LoopAnalysisManager>          TheLAM;
static unique_ptr<FunctionAnalysisManager>      TheFAM;
static unique_ptr<CGSCCAnalysisManager>         TheCGAM;
static unique_ptr<ModuleAnalysisManager>        TheMAM;
static unique_ptr<PassInstrumentationCallbacks> ThePIC;
static unique_ptr<StandardInstrumentations>     TheSI;
static map<string, unique_ptr<PrototypeAST>>    FunctionProtos;
static ExitOnError                              ExitOnErr;
```

**`TheJIT`** is the ORC JIT instance. It lives for the whole session. Compiled modules are added to it; when a symbol is looked up (e.g. `__anon_expr`), the JIT compiles to native code and returns the address.

**`TheFPM`** is the `FunctionPassManager` — the object that runs the optimisation pipeline over each function after codegen.

**`TheLAM` / `TheFAM` / `TheCGAM` / `TheMAM`** are analysis managers for different granularities of IR (loops, functions, call-graph SCCs, modules). They cache analysis results and share them between passes. They are cross-registered so a function pass that needs loop information can reach the loop analysis manager.

**`ThePIC` / `TheSI`** are pass instrumentation hooks required by the new LLVM pass manager. `StandardInstrumentations` registers timing and printing callbacks.

**`FunctionProtos`** is a persistent prototype registry. It stores a `PrototypeAST` for every function that has been declared or defined. This is needed because the JIT takes ownership of the module after each compilation — so a function defined earlier is no longer in `TheModule`. When a later expression calls it, `FunctionProtos` lets us re-emit a `declare` in the current module.

**`ExitOnErr`** wraps JIT operations that return `Expected<T>`. If the JIT encounters an unrecoverable error it calls `exit()` rather than silently returning a bad value.

## The Per-Module Lifetime

In chapter 5, one module accumulated everything. The JIT changes that. When a compiled module is handed to the JIT, the JIT takes ownership — the old `TheModule` pointer is gone. We have to create a fresh module for each new top-level input.

`InitializeModuleAndManagers()` now does two things: create the module and rebuild the pass pipeline:

```cpp
static void InitializeModuleAndManagers() {
  TheContext = make_unique<LLVMContext>();
  TheModule  = make_unique<Module>("PyxcJIT", *TheContext);
  TheModule->setDataLayout(TheJIT->getDataLayout());
  Builder    = make_unique<IRBuilder<>>(*TheContext);

  TheFPM = make_unique<FunctionPassManager>();
  // ... analysis managers ...

  TheFPM->addPass(InstCombinePass());
  TheFPM->addPass(ReassociatePass());
  TheFPM->addPass(GVNPass());
  TheFPM->addPass(SimplifyCFGPass());

  PassBuilder PB;
  PB.registerModuleAnalyses(*TheMAM);
  PB.registerFunctionAnalyses(*TheFAM);
  PB.crossRegisterProxies(*TheLAM, *TheFAM, *TheCGAM, *TheMAM);
}
```

`setDataLayout` informs the module of the JIT's target — the host machine's pointer size, alignment rules, and endianness. Without it, codegen might emit incorrectly-sized types.

The function is called once at startup, and again each time a module is handed to the JIT.

## The Optimisation Pipeline

Four passes run on each function immediately after `verifyFunction`, inside `FunctionAST::codegen`:

```cpp
verifyFunction(*TheFunction);
TheFPM->run(*TheFunction, *TheFAM);
```

| Pass | What it does |
|---|---|
| `InstCombinePass` | Peephole rewrites: algebraic identities (`a+0→a`), strength reduction, constant propagation within a single basic block |
| `ReassociatePass` | Reorder commutative operations to expose more constant folding: `(x+2)+3` becomes `x+(2+3)` which becomes `x+5` |
| `GVNPass` | Global Value Numbering: eliminate redundant computations and loads across basic blocks; recognises that two sub-expressions produce the same value |
| `SimplifyCFGPass` | Remove unreachable basic blocks and merge blocks that always fall through to the next one |

The passes run in this order intentionally: InstCombine normalises instructions, Reassociate rearranges them into canonical forms, GVN then finds common sub-expressions in the normalised IR, and SimplifyCFG cleans up any structural dead code that earlier passes exposed.

## getFunction and the Cross-Module Problem

In chapter 5, `CallExprAST::codegen` called `TheModule->getFunction(Callee)` directly. That breaks with per-module lifetime: if `foo` was compiled in a previous module, the current module has no record of it.

The solution is a new `getFunction` helper:

```cpp
Function *getFunction(string Name) {
  if (auto *F = TheModule->getFunction(Name))
    return F;

  auto FI = FunctionProtos.find(Name);
  if (FI != FunctionProtos.end())
    return FI->second->codegen();

  return nullptr;
}
```

Fast path: the function is already in the current module (either just defined, or already re-emitted as a `declare`). Slow path: look up the `PrototypeAST` in `FunctionProtos` and call `codegen()` on it, which emits a fresh `declare double @foo(double)` in the current module. The JIT then resolves that `declare` to the already-compiled body at link time.

This is why `HandleExtern` saves into `FunctionProtos`:

```cpp
FunctionProtos[ProtoAST->getName()] = move(ProtoAST);
```

And why `FunctionAST::codegen` registers the prototype before doing anything else:

```cpp
auto &P = *Proto;
FunctionProtos[Proto->getName()] = move(Proto);
Function *TheFunction = getFunction(P.getName());
```

## Executing Top-Level Expressions

`HandleTopLevelExpression` does everything it did in chapter 5 — codegen, optimise, print IR — and then executes the result:

```cpp
// Create a ResourceTracker scoped to this expression.
auto RT = TheJIT->getMainJITDylib().createResourceTracker();

// Hand the module to the JIT; reinitialise for the next input.
auto TSM = ThreadSafeModule(move(TheModule), move(TheContext));
ExitOnErr(TheJIT->addModule(move(TSM), RT));
InitializeModuleAndManagers();

// Look up the compiled function and call it.
auto ExprSymbol = ExitOnErr(TheJIT->lookup("__anon_expr"));
double (*FP)() = ExprSymbol.toPtr<double (*)()>();
fprintf(stderr, "Evaluated to %f\n", FP());

// Free the compiled code.
ExitOnErr(RT->remove());
```

**`ThreadSafeModule`** wraps the module and its context together so the JIT can safely access them from its own threads.

**`ResourceTracker`** is the key to cleanup. Rather than calling `eraseFromParent()` like chapter 5 did, we attach the anonymous expression to a `ResourceTracker` and call `RT->remove()` after execution. This tells the JIT to free all compiled code, object files, and symbol table entries associated with this expression. Named functions — `def foo` — are added without a tracker and stay in the JIT permanently.

**`ExprSymbol.toPtr<double(*)()>()`** gets the native machine address of the compiled function and casts it to a callable C function pointer. Calling it runs the compiled native code directly.

## The Runtime Library

Chapter 6 adds two built-in functions that Pyxc programs can call via `extern def`:

```cpp
extern "C" DLLEXPORT double putchard(double X) {
  fputc((char)X, stderr);
  return 0;
}

extern "C" DLLEXPORT double printd(double X) {
  fprintf(stderr, "%f\n", X);
  return 0;
}
```

They are compiled into the `pyxc` binary itself and exported with C linkage. The JIT resolves `extern def putchard(x)` to this address through the process's dynamic symbol table — no linking step required.

`DLLEXPORT` is a no-op on macOS and Linux but is required on Windows where symbols are not exported by default.

## Build and Run

```bash
cd code/chapter-06
cmake -S . -B build && cmake --build build
./build/pyxc
```

## Try It

```
ready> extern def sin(x)
Parsed an extern.
declare double @sin(double)

ready> sin(1)
Parsed a top-level expression.
define double @__anon_expr() {
entry:
  %calltmp = call double @sin(double 1.000000e+00)
  ret double 0x3FEAED548F090CEE
}
Evaluated to 0.841471
```

`sin(1)` runs immediately and prints the result. The hex constant in `ret double 0x3FEAED548F090CEE` is the IEEE 754 encoding of `≈ 0.841471` — the JIT used constant folding on the argument but left the return value as-is since it's the result of an opaque extern call.

```
ready> def foo(x): return sin(x)*sin(x)+cos(x)*cos(x)
Parsed a function definition.
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

ready> foo(4)
Evaluated to 1.000000
```

`sin²(x) + cos²(x) = 1` for any x. The call duplication (two calls to `sin` and two to `cos`) is a known limitation — the pass manager cannot eliminate duplicate calls to extern functions without alias analysis knowing they have no side effects. That's in the Known Limitations section below.

```
ready> def test(x): return (1+2+x)*(x+(1+2))
Parsed a function definition.
define double @test(double %x) {
entry:
  %addtmp = fadd double %x, 3.000000e+00
  %multmp = fmul double %addtmp, %addtmp
  ret double %addtmp
}
```

Six source operations collapsed to two IR instructions:
- Both `1+2` sub-expressions folded to `3.0` at IR construction time (constant folding in `IRBuilder`).
- `ReassociatePass` canonicalised `(x+3)*(x+3)` so both factors are the same SSA value.
- `GVNPass` eliminated the duplicate `x+3` computation, leaving a single `%addtmp`.
- One `fmul double %addtmp, %addtmp` computes the square.

```
ready> extern def printd(x)
Parsed an extern.
declare double @printd(double)

ready> printd(42)
42.000000
Evaluated to 0.000000
```

`42.000000` is printed by `printd`'s own `fprintf`. `Evaluated to 0.000000` is the JIT executing the `__anon_expr` wrapper and printing `printd`'s return value (always `0.0`).

## What We Built

| Piece | What it does |
|---|---|
| `PyxcJIT` | ORC JIT instance; compiles modules to native code, resolves symbols |
| `FunctionPassManager` | Runs the optimisation pipeline over each function after codegen |
| `InstCombinePass` | Peephole rewrites and algebraic simplification |
| `ReassociatePass` | Reorders commutative ops to expose constant folding |
| `GVNPass` | Eliminates redundant computations across basic blocks |
| `SimplifyCFGPass` | Removes dead blocks and merges trivial branches |
| Analysis managers | Cache analysis results; cross-registered so passes share information |
| `FunctionProtos` | Persistent prototype registry; enables cross-module function calls |
| `getFunction()` | Resolves a name: current module first, then re-emit from `FunctionProtos` |
| `InitializeModuleAndManagers()` | Creates a fresh module + pipeline after each JIT transfer |
| `ThreadSafeModule` | Wraps module + context for safe transfer to the JIT |
| `ResourceTracker` | Scopes JIT memory for anonymous expressions; freed after execution |
| `ExitOnErr` | Terminates cleanly on unrecoverable JIT errors |
| `putchard` / `printd` | Built-in runtime functions callable via `extern def` |

## Known Limitations

- **Duplicate extern calls not eliminated.** `sin(x)*sin(x)` calls `sin` twice. GVN cannot merge calls to extern functions without alias information marking them as pure (no side effects). A later chapter can annotate externs with LLVM attributes.
- **No local variables.** `NamedValues` still only holds function parameters. Mutable locals require `alloca`/`store`/`load` and `mem2reg`. A later chapter adds these.
- **No control flow.** `if`/`else` and loops are not yet supported. A later chapter adds them.
- **Single-expression function bodies only.** The `def foo(x): return expr` syntax allows exactly one expression after `return`. Multiple statements and sequencing come later.

## What's Next

The REPL now executes code. Chapter 7 adds control flow — `if`/`then`/`else` and `for` loops — which requires introducing basic block branching and phi nodes into the codegen layer.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.
