---
description: "Add ORC JIT and an optimisation pass pipeline: top-level expressions now execute immediately and functions come out smaller."
---
# 6. Pyxc: JIT and Optimisation

## Where We Are

Chapter 5 produces correct IR, but you have to read it — nothing runs. Define `test` and call it with `2`, and you get IR back with no result:

```python
ready> def test(x): return (1+2+x)*(x+(1+2))
Parsed a function definition.
```

```llvm
define double @test(double %x) {
entry:
  %addtmp = fadd double 3.000000e+00, %x
  %addtmp1 = fadd double %x, 3.000000e+00
  %multmp = fmul double %addtmp, %addtmp1
  ret double %multmp
}
```

```python
ready> test(2)
Parsed a top-level expression.
```

```llvm
define double @__anon_expr() {
entry:
  %calltmp = call double @test(double 2.000000e+00)
  ret double %calltmp
}
```

Two problems. First, `test(2)` doesn't evaluate — you see the IR for the call but no result. Second, the IR for `test` isn't as clean as it could be: `(1+2+x)*(x+(1+2))` produces two separate `fadd` instructions even though both sides of the multiply are the same expression `x+3`.

By the end of this chapter, calling `test(2)` prints the answer:

```python
ready> test(2)
Parsed a top-level expression.
```

```llvm
define double @__anon_expr() {
entry:
  %calltmp = call double @test(double 2.000000e+00)
  ret double %calltmp
}
```

```bash
Evaluated to 25.000000
```

And `test` itself comes out of the optimiser with the redundant computation eliminated:

```python
ready> def test(x): return (1+2+x)*(x+(1+2))
Parsed a function definition.
```

```llvm
define double @test(double %x) {
entry:
  %addtmp = fadd double %x, 3.000000e+00
  %multmp = fmul double %addtmp, %addtmp
  ret double %addtmp
}
```

Two instructions instead of three. One `fadd` instead of two — the optimiser recognised that both factors are `x+3` and computed it once.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-06
```

## What is ORC JIT?

ORC stands for **On-Request Compilation**. It is LLVM's current JIT framework — a library for building JIT compilers, not a single fixed JIT.

The key idea is lazy compilation: ORC doesn't compile everything upfront. It compiles a function when something first asks for its address. You add a module to the JIT and get back a handle; the native code is produced on demand when a symbol is first looked up.

ORC is also composable. It is built from layers — an IR layer that runs optimisation passes, an object layer that handles machine code, a dynamic linker layer that resolves symbols. For our purposes we use `PyxcJIT`, a thin wrapper around ORC's `LLJIT` that sets up these layers with sensible defaults for a REPL:

```cpp
static unique_ptr<PyxcJIT> TheJIT;
// ...
TheJIT = ExitOnErr(PyxcJIT::Create());
```

That one line gives us a fully functional JIT for the host machine. `PyxcJIT::Create()` initialises the native target, picks the right code model, sets up the dynamic linker, and returns a JIT instance ready to accept modules.

The dynamic linker part is worth pausing on. `pyxc` is a C++ program linked against the C standard library, so `sin`, `cos`, `printf`, and every other standard function are already loaded into the `pyxc` process when it starts. When it can't resolve a symbol internally, ORC searches `pyxc`'s own symbol table — and finds them there. When Pyxc code calls `sin`, the JIT looks it up in `pyxc`'s symbol table and uses the address directly. No `#include`, no link flags, no explicit registration. We'll see this in "Try It" below.

## One Module Per Input

The driver for this design is anonymous top-level expressions.

When you type `test(2)`, Pyxc wraps it in a zero-argument function called `__anon_expr`, compiles it, runs it, and then wants to delete the compiled native code — you don't want `__anon_expr` accumulating in the JIT forever.

You could remove `__anon_expr` from the module's IR with `eraseFromParent()` before handing the module to the JIT, but then the JIT never compiles it — so you can't execute it. You need to compile it first, then free the resulting native code.

The JIT frees native code via `ResourceTracker::remove()`. A `ResourceTracker` scopes everything compiled from a single `addModule` call — the object file, the symbol table entries, the executable memory. `RT->remove()` frees all of that at once. It does not operate at function granularity; it operates at module granularity.

That's the constraint. If `__anon_expr` and `test` were compiled from the same module, `RT->remove()` would free `test`'s native code too. Calling `test` afterwards would be a crash.

The solution is to give each top-level input its own module. `def test` gets compiled into module A, handed to the JIT, and stays there permanently. `test(2)` gets compiled into module B, attached to a `ResourceTracker`, and freed immediately after execution. Module A is untouched.

If Pyxc didn't support bare top-level expressions at all — only `def` and `extern` — none of this would be necessary. One module for the whole session would work fine. The per-module lifetime is the price of a REPL that evaluates expressions immediately.

This is why `InitializeModuleAndManagers()` is called both at startup and after every JIT transfer:

```cpp
// Hand the module to the JIT.
ExitOnErr(TheJIT->addModule(ThreadSafeModule(move(TheModule), move(TheContext))));

// Start fresh for the next input.
InitializeModuleAndManagers();
```

## The Optimisation Pipeline

LLVM ships dozens of optimisation passes. They are not enabled by default — there is no "turn on all optimisations" switch, and that's intentional. Different use cases need different passes. An embedded target with tight code size limits wants different transforms than a high-performance server. A debug build wants no transforms at all. You pick the passes that make sense for your language and your users.

### How it fits together

Each time a function is codegenned, the pipeline runs on it immediately:

```
codegen produces IR
        │
        ▼
  verifyFunction         ← checks IR is well-formed
        │
        ▼
  FunctionPassManager    ← runs each pass in order
        │
   ┌────┴──────────────────────────────────┐
   │  for each pass:                       │
   │                                       │
   │   pass asks AnalysisManager           │
   │       "is this value used elsewhere?" │
   │       "have we seen this computation?"│
   │                                       │
   │   AnalysisManager checks its cache    │
   │       hit  → return cached result     │
   │       miss → compute once, cache it   │
   │                                       │
   │   pass uses the answer to rewrite IR  │
   └───────────────────────────────────────┘
        │
        ▼
   optimised IR handed to JIT
```

The analysis managers sit alongside the pass manager as a shared cache. Passes don't compute things themselves — they ask, and the manager either returns a cached answer or computes it once and stores it for the next pass that asks the same question.

### Analysis Managers

`InitializeModuleAndManagers` creates four analysis managers: `TheFAM` (per-function), `TheLAM` (per-loop), `TheCGAM` (per-call-graph), `TheMAM` (per-module). For our four passes, `TheFAM` is the one doing work — the others are registered because the pass manager requires all four tiers to be present. If a pass requests an analysis and the manager isn't there, LLVM asserts and crashes.

### Debug Logging

`ThePIC` and `TheSI` are how you ask LLVM to print the IR before and after each pass. Change `/*DebugLogging*/ false` to `true` in `InitializeModuleAndManagers` and every pass will dump the IR as it runs — useful when you want to see exactly which pass is responsible for a transformation. With `false`, they register but do nothing.

### The Four Passes

Four passes run on each function, in this order:

```cpp
verifyFunction(*TheFunction);
TheFPM->run(*TheFunction, *TheFAM);
```

| Pass | What it does |
|---|---|
| `InstCombinePass` | Simplifies individual instructions: `x * 1 → x`, `x + 0 → x`, and similar obvious rewrites |
| `ReassociatePass` | Reorders additions and multiplications so constants end up together: `(x+2)+3` becomes `x+(2+3)`, which then collapses to `x+5` |
| `GVNPass` | Finds places where the same value is computed twice and removes the duplicate. This is what eliminates the second `fadd` in `test` |
| `SimplifyCFGPass` | Removes dead branches and unreachable blocks — not relevant yet, but essential once we add `if`/`while` |

Note that `1+2` collapsing to `3` isn't done by any of these passes — `IRBuilder` does it automatically as it constructs the IR. That's why `(1+2+x)` already shows `3.000000e+00` in the output before any pass runs.

## getFunction and the Cross-Module Problem

In chapter 5, `CallExprAST::codegen` called `TheModule->getFunction(Callee)` directly. That breaks with per-module lifetime: if `test` was compiled and its module handed to the JIT, the current module has no record of `test`. A call to `test(2)` would fail.

The solution is a persistent prototype registry, `FunctionProtos`, and a helper that uses it:

```cpp
Function *getFunction(string Name) {
  // Fast path: already in the current module.
  if (auto *F = TheModule->getFunction(Name))
    return F;

  // Slow path: re-emit a declaration from the saved prototype.
  auto FI = FunctionProtos.find(Name);
  if (FI != FunctionProtos.end())
    return FI->second->codegen();

  return nullptr;
}
```

When `test` is compiled, its `PrototypeAST` is saved into `FunctionProtos`. When a new module is created and `test(2)` is codegenned, `getFunction` doesn't find `test` in the fresh module — so it calls `codegen()` on the saved prototype, emitting a `declare double @test(double)` in the current module. The JIT then resolves that `declare` to the already-compiled body.

`extern def` declarations follow the same pattern:

```cpp
// In HandleExtern:
FunctionProtos[ProtoAST->getName()] = move(ProtoAST);
```

And every function definition registers itself before codegen:

```cpp
// In FunctionAST::codegen:
auto &P = *Proto;
FunctionProtos[Proto->getName()] = move(Proto);
Function *TheFunction = getFunction(P.getName());
```

## Executing Top-Level Expressions

`HandleTopLevelExpression` codegens and optimises the anonymous function exactly as before, then executes it:

```cpp
// Scope this expression's compiled code to a ResourceTracker.
auto RT = TheJIT->getMainJITDylib().createResourceTracker();

// Transfer the module to the JIT; create a fresh one for the next input.
auto TSM = ThreadSafeModule(move(TheModule), move(TheContext));
ExitOnErr(TheJIT->addModule(move(TSM), RT));
InitializeModuleAndManagers();

// Find the compiled function by name and call it.
auto ExprSymbol = ExitOnErr(TheJIT->lookup("__anon_expr"));
double (*FP)() = ExprSymbol.toPtr<double (*)()>();
fprintf(stderr, "Evaluated to %f\n", FP());

// Free the compiled code for this expression.
ExitOnErr(RT->remove());
```

**`ThreadSafeModule`** packages the module and its context together for safe handoff to the JIT's internal threads.

**`ResourceTracker`** is how we delete anonymous expressions precisely. Named functions (`def test`) are added to the JIT's main dylib without a tracker — they stay compiled permanently. Anonymous expressions are attached to a tracker; `RT->remove()` tells the JIT to free their compiled code, object file, and symbol table entries the moment we're done with them. This replaces the `eraseFromParent()` call from chapter 5.

**`ExprSymbol.toPtr<double(*)()>()`** gets the native machine-code address of the compiled `__anon_expr` function and casts it to a C function pointer. `FP()` executes the compiled native code directly on the host CPU.

## The Runtime Library

Chapter 6 adds two built-in functions callable from Pyxc via `extern def`:

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

They are compiled into the `pyxc` binary with C linkage. Because ORC's dynamic linker searches the host process's symbol table, `extern def printd(x)` resolves to this function automatically at runtime — no registration required.

`DLLEXPORT` is a no-op on macOS and Linux. On Windows, symbols are not exported from executables by default, so the macro expands to `__declspec(dllexport)` to make them visible to the JIT's linker.

## Build and Run

```bash
cd code/chapter-06
cmake -S . -B build && cmake --build build
./build/pyxc
```

## Try It

```llvm
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

Notice what just happened. We declared `sin` as an extern and immediately called it — and it worked. We didn't link against anything. We didn't pass any flags. We didn't register `sin` anywhere.

Here's why. The `pyxc` binary is itself a C++ program, and like every C++ program on your system it is linked against the C standard library (`libc`). That library — which contains `sin`, `cos`, `sqrt`, `printf`, and hundreds of other functions — is loaded into the process when `pyxc` starts. All of its symbols are sitting in the process's memory, visible in its symbol table.

When the JIT looks up `sin`, it searches that same symbol table and finds the address of the real `sin` that's already loaded. No separate linking step, no `#include`, no `-lm`. The function was already there. The JIT just had to ask.

This is true of any function in any shared library that is already loaded into the process. On macOS and Linux, the C standard library and the system math library are always loaded. Every C library function — `cos`, `tan`, `sqrt`, `printf`, `malloc`, all of them — is available to Pyxc programs this way, for free. The hex value `0x3FEAED548F090CEE` is the IEEE 754 encoding of `sin(1) ≈ 0.841471`.

```llvm
ready> extern def cos(x)
Parsed an extern.
declare double @cos(double)

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

`sin²(x) + cos²(x) = 1` for any x — the Pythagorean identity. The JIT compiled `foo`, looked up the native `sin` and `cos`, and executed the whole thing. The call duplication (two calls to `sin`, two to `cos`) is a known limitation covered below.

```llvm
ready> def test(x): return (1+2+x)*(x+(1+2))
Parsed a function definition.
define double @test(double %x) {
entry:
  %addtmp = fadd double %x, 3.000000e+00
  %multmp = fmul double %addtmp, %addtmp
  ret double %addtmp
}
```

Six source operations, two IR instructions. `IRBuilder` folded `1+2` to `3.0` for free at construction time. `ReassociatePass` and `GVNPass` recognised that both factors of the multiply are `x+3` and eliminated the duplicate `fadd`, leaving `%addtmp * %addtmp`.

```llvm
ready> test(2)
Evaluated to 25.000000
```

`(3+2)*(2+3) = 5*5 = 25`. Correct.

```python
ready> extern def printd(x)
Parsed an extern.
```

```llvm
declare double @printd(double)
```

```python
ready> printd(42)
42.000000
Evaluated to 0.000000
```

`42.000000` is printed by `printd`'s own `fprintf`. `Evaluated to 0.000000` is the JIT printing `printd`'s return value (always `0.0`) after executing the `__anon_expr` wrapper.

## What We Built

| Piece | What it does |
|---|---|
| `PyxcJIT` | Thin ORC `LLJIT` wrapper; native target init, IR layer, object layer, dynamic linker |
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

- **Duplicate extern calls not eliminated.** `sin(x)*sin(x)` calls `sin` twice. GVN cannot merge calls to extern functions without alias information marking them as pure (no side effects). LLVM function attributes can express this; a later chapter can add them.
- **No local variables.** `NamedValues` still only holds function parameters. Mutable locals require `alloca`/`store`/`load` and `mem2reg`. A later chapter adds these.
- **No control flow.** `if`/`else` and loops are not yet supported. A later chapter adds them.
- **Single-expression function bodies only.** The `def foo(x): return expr` syntax allows exactly one expression after `return`. Multiple statements and sequencing come later.

## What's Next

Chapter 7 adds a command-line interface — `pyxc run script.pyxc` and `pyxc emit-ir script.pyxc` — so the compiler can read source files instead of just the REPL. That's the foundation control flow and real programs need.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.
