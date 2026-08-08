---
description: "Add ORC JIT and an optimization pass pipeline: top-level expressions now execute immediately and functions come out smaller. Also adds the extern keyword for calling real C library functions."
---
# 7. pyxc: JIT and Optimization

## Where We Are

I got the compiler to produce IR in [Chapter 6](chapter-06.md), but it doesn't run anything yet. For example:

<!-- code-merge:start -->
```pyxc
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

```pyxc
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

`foo(2)` doesn't evaluate — you see the IR for the call but no result. No bueno. Furthermore, the IR for `foo` isn't as clean as it could be: `(1+2+x)*(x+(1+2))` produces two separate `fadd` instructions even though both sides of the multiply are the same expression `x+3`.

By the end of this chapter, calling `foo(2)` prints the answer:

<!-- code-merge:start -->
```pyxc
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
```pyxc
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

Clean. Two instructions instead of three. One `fadd` instead of two — the optimizer recognized that both factors are `x+3` and computed it once.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-07
```

## Let's ORC JIT.

ORC stands for **On-Request Compilation**. It's LLVM's JIT framework — a library for building JIT compilers, not a single fixed JIT. ([ORCv2 docs](https://llvm.org/docs/ORCv2.html))

ORC materializes code on demand. Adding a module makes its symbols available to the JIT, and machine code is generally generated when a symbol is looked up (for example, `lookup("__anon_expr")`). I use `LLJIT` via `PyxcJIT`, while the framework also provides a lazier variant (`LLLazyJIT`) that can defer work even more aggressively until first use. *LL* stands for low-level.

`PyxcJIT` ([include/PyxcJIT.h](https://github.com/alankarmisra/pyxc-llvm-tutorial/blob/main/code/include/PyxcJIT.h)) is a thin wrapper around ORC's [LLJIT](https://llvm.org/docs/ORCv2.html#lljit-and-lllazyjit). It is created once in `main()`. Let's look at the additions to `main()`:

```cpp
// the ORC JIT instance
static unique_ptr<PyxcJIT> TheJIT;       
// terminates on unrecoverable JIT error
static ExitOnError ExitOnErr;          
...
// This readies LLVM to convert the IR into an internal representation of the instructions for the target machine.
InitializeNativeTarget();
// This readies LLVM to convert text assembly into an internal representation of the instructions for the target machine.
InitializeNativeTargetAsmParser();
// This readies LLVM to convert the internal representation of the instructions into actual machine code.
InitializeNativeTargetAsmPrinter();
TheJIT = ExitOnErr(PyxcJIT::Create());
InitializeModuleAndManagers();
```

## The Optimization Pipeline

Remember how LLVM couldn't figure out that `(1+2+x)` and `(x+(1+2))` are the same thing, i.e. `(x+3)`? I'm going to fix that. LLVM passes fall into two categories: **per-module passes** that see everything in a module at once, and **per-function passes** that operate on one function at a time. I use per-function passes, applied immediately as each function is compiled — so the user gets optimized code for every definition they type without waiting for a full program to accumulate.

[`FunctionPassManager`](https://llvm.org/docs/NewPassManager.html) (TheFPM) sequences these passes in order, running each one on the function and updating it in place. LLVM ships [dozens of passes](https://llvm.org/docs/Passes.html). I'll add three specific passes which are quick wins — they catch easy improvements without the cost of a full optimization pipeline:

| Pass | What it does |
|---|---|
| `InstCombinePass` | Simplifies individual instructions: `x * 1 → x`, `x + 0 → x`, and similar peephole rewrites |
| `ReassociatePass` | Reorders additions and multiplications so constants end up together: `(x+2)+3` becomes `x+(2+3)`, which then collapses to `x+5` |
| `GVNPass` | Finds places where the same value is computed twice and removes the duplicate. This is what eliminates the second `fadd` in `foo` |

Note that `1+2` collapsing to `3` isn't done by any of these passes — `IRBuilder` does it automatically as it constructs the IR. That's why `(1+2+x)` already shows `3.000000e+00` in the output before any pass runs.

## Initialize Module And Managers

`InitializeModuleAndManagers()` grows out of the same function from [chapter 5](chapter-06.md). It is called once at startup and again after each module is handed to the JIT. It has two distinct jobs.

**1. Reset the module**

Same as chapter 5's version, with one new line:

```cpp
TheContext = make_unique<LLVMContext>();
TheModule  = make_unique<Module>("PyxcJIT", *TheContext);
TheModule->setDataLayout(TheJIT->getDataLayout()); // new: ties the module to the host machine
Builder    = make_unique<IRBuilder<>>(*TheContext);
```

`setDataLayout` tells the module how the JIT lays out data for the host machine — pointer widths, type sizes, and so on. You don't need to think about this unless you're targeting a different platform than the one you're compiling on.

**2. Wire up the pass manager infrastructure**
This is required plumbing for the pass framework — it doesn't optimize anything itself:

```cpp
// stores results of loop analyses
static unique_ptr<LoopAnalysisManager>     TheLAM;    
// stores results of function analyses
static unique_ptr<FunctionAnalysisManager> TheFAM;    
// stores results of call-graph analyses
static unique_ptr<CGSCCAnalysisManager>    TheCGAM;   
// stores results of module analyses
static unique_ptr<ModuleAnalysisManager>   TheMAM;    
...              
// analysis results scoped to a loop
TheLAM  = make_unique<LoopAnalysisManager>();     
// analysis results scoped to a function
TheFAM  = make_unique<FunctionAnalysisManager>(); 
// analysis results scoped to a call-graph cluster ie graphs representing 
// functions calling each other to find interdependencies
TheCGAM = make_unique<CGSCCAnalysisManager>();    
// analysis results scoped to the whole module
TheMAM  = make_unique<ModuleAnalysisManager>();   

PassBuilder PB;
PB.registerModuleAnalyses(*TheMAM);
PB.registerCGSCCAnalyses(*TheCGAM);
PB.registerFunctionAnalyses(*TheFAM);
PB.registerLoopAnalyses(*TheLAM);
// Wires all the analysis layers/tiers together so a pass at any tier 
// can request analysis results from any other
PB.crossRegisterProxies(*TheLAM, *TheFAM, *TheCGAM, *TheMAM);
```

**3. Optimization: the per-function pipeline**

This is the only part that actually changes IR:

```cpp
// runs optimizations per function
static unique_ptr<FunctionPassManager>     TheFPM;       
...
// in InitializeModuleAndManagers()
// sequences passes over a function
TheFPM  = make_unique<FunctionPassManager>();
...
if (OptLevel != 0) {
TheFPM->addPass(InstCombinePass());
TheFPM->addPass(ReassociatePass());
TheFPM->addPass(GVNPass());
}
```

`OptLevel` is a command-line flag covered in [Command-Line Parsing](#command-line-parsing) — for now, read this as "skip optimizations if the user asked for none."

**When it runs**

Right after each function is generated and verified:

```cpp
// In FunctionAST::codegen, after verifyFunction:
TheFPM->run(*TheFunction, *TheFAM);
```

Every function definition typed into the REPL is optimized immediately, without waiting for a whole file.

## Executing Top-Level Expressions

`HandleTopLevelExpression` codegens and optimizes the anonymous function exactly as before, then executes it:

```cpp
if (auto *FnIR = FnAST->codegen()) {
  FnIR->print(errs());

  // Track this module so I can free it immediately after execution.
  auto RT = TheJIT->getMainJITDylib().createResourceTracker();

  // Transfer the module to the JIT. TheModule is now owned by the JIT.
  auto TSM = ThreadSafeModule(move(TheModule), move(TheContext));
  ExitOnErr(TheJIT->addModule(move(TSM), RT));

  // Create a fresh module for the next input. See: One Module Per Compilation Unit.
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

**`RT->remove()`** frees the object file and executable memory for `__anon_expr`. This replaces the `eraseFromParent()` call from [chapter 5](chapter-06.md) — instead of removing the IR before JIT, I compile it first and then free the resulting native code.

> `Expected<T>` is LLVM's error-returning wrapper — `ExitOnErr` unwraps it or terminates the process on failure. JIT calls that can fail (`addModule`, `lookup`, `RT->remove()`) return it; calls that can't (`createResourceTracker()`) return plain values.

Named functions (`def foo`) are added to the JIT's internal registry without a tracker — they stay compiled permanently.

## Calling Functions I Didn't Write

Now that expressions actually execute, calling out to code I didn't write becomes worth doing — the C standard library's `sin` and `cos`, or, once I add a small runtime library later in this chapter, functions I wrote myself in C++. `def` can't express this: it always expects a body, and I don't have one to give it — the whole point is that the implementation lives somewhere else. I need a keyword that says "this function exists, here's its signature, trust me" and stops there.

A new token:

```cpp
enum Token {
  ...
  tok_def    = -4,
  tok_extern = -5,
  ...
};
```

Added to the keyword table right alongside `def` and `return`:

```cpp
static map<string, Token> Keywords = {
    {"def", tok_def}, {"extern", tok_extern}, {"return", tok_return}};
```

And a grammar rule — a prototype with no body:

```ebnf
external       = "extern" "def" function-signature ;
top-level-item = function-definition | external | top-level-expression ;
```

The parser is almost embarrassingly small, because all the hard work — parsing a signature — was already built for `def` back in [Chapter 2](chapter-02.md):

```cpp
/// external
///   = "extern" "def" prototype
static unique_ptr<PrototypeAST> ParseExtern() {
  getNextToken(); // eat extern.
  if (CurTok != tok_def)
    return LogErrorP("Expected `def` after extern.");
  getNextToken(); // eat def
  return ParsePrototype();
}
```

`extern` wants the same thing `def` wants, up to the point where `def` would need a `:` and a body. It stops right there.

Codegen doesn't need anything new either. `PrototypeAST::codegen()` (from [Chapter 6](chapter-06.md)) already builds exactly what I want: a `Function*` with the right signature and no body, which LLVM prints as a `declare`:

```llvm
declare double @sin(double)
```

`FunctionAST::codegen()` is the one that adds a body — skip it, and a `declare` is all you get. That's what "the implementation lives somewhere else" means in LLVM terms.

The driver's `HandleExtern` ties parsing and codegen together:

```cpp
static void HandleExtern() {
  auto ProtoAST = ParseExtern();

  if (!ProtoAST || (CurTok != tok_eol && CurTok != tok_eof)) {
    if (ProtoAST)
      LogError(("Unexpected " + FormatTokenForMessage(CurTok)).c_str());
    SynchronizeToLineBoundary();
    return;
  }

  if (auto *FnIR = ProtoAST->codegen()) {
    Log("Parsed an extern.\n");
    FnIR->print(errs());
    FunctionProtos[ProtoAST->getName()] = std::move(ProtoAST);
  }
}
```

That last line — saving the prototype into `FunctionProtos` — doesn't matter yet. It will, a couple of sections from now, once I explain why one module isn't enough to get through a whole session. For now, the visible result is just this:

```pyxc
ready> extern def sin(x)
Parsed an extern.
declare double @sin(double)
```

A signature, no body, ready to be resolved against the real `sin` at JIT time. `MainLoop` dispatches to it the same way it dispatches to `HandleDefinition`:

```cpp
switch (CurTok) {
case tok_def:
  HandleDefinition();
  break;
case tok_extern:
  HandleExtern();
  break;
default:
  HandleTopLevelExpression();
  break;
}
```

## One Module Per Compilation Unit

When you type `foo(2)`, Pyxc wraps it in a zero-argument function called `__anon_expr`, compiles it, runs it, and then frees it — anonymous expressions shouldn't accumulate in the JIT forever. The JIT frees native code at the module level via `ResourceTracker::remove()`, so `__anon_expr` needs its own module or removing it would take `foo` with it.

But this means I should put anonymous functions in their own module instead of pooling them with regular functions, so that `ResourceTracker::remove()` doesn't land up nuking ALL functions. This chapter uses a more naive strategy: every extern, every function definition and every top-level expression gets its own fresh module. This way, nuking a module containing an anonymous function only nukes that function and no other.

A smarter approach would give anonymous expressions their own modules and batch named functions together — but the simple version is easier to follow, and I'll revisit it later.

One side effect: LLVM forbids redefining a function name even across modules, so typing `def foo` twice is an error. I could support it for the sake of the REPL by nuking older versions if I encounter a redefinition, but then I'd need to fork the redefinition handling for REPL and non-REPL instances. In the latter case, a redefinition is a potential error. I'll deal with this in a later chapter. 

It must be dawning on you by now that each time I do a "What if?" exercise to something more *intuitive*, the feature-set grows just a tad bit. This can be a slippery slope and holy wars are fought over what one considers *intuitive*. I will be fighting no wars. I will decide what I want the language to look like, and should you disagree, you will fork your own.

`InitializeModuleAndManagers()` is consequently called both at startup and after every module transfer, i.e. each time I hand over a module to the JIT:

```cpp
// Hand the module to the JIT.
ExitOnErr(TheJIT->addModule(ThreadSafeModule(move(TheModule), move(TheContext))));

// Start fresh for the next input.
InitializeModuleAndManagers();
```

`ThreadSafeModule` packages the module and its context together for safe handoff to the JIT's internal threads.

## `getFunction` and the Cross-Module Problem

In [chapter 5](chapter-06.md), in order to find a function, I called `TheModule->getFunction(Callee)`. That breaks with per-module lifetime: if `foo` was compiled and its module handed to the JIT, the current module has no record of `foo`. A call to `foo(2)` would fail with "Unknown function referenced."

The solution is a persistent prototype registry which I call `FunctionProtos`, and a helper `getFunction()` that uses it:

```cpp
// persistent prototype registry
static map<string, unique_ptr<PrototypeAST>> FunctionProtos; 
...
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

1. `def foo` is compiled into module `m1`. Its `PrototypeAST` is saved into `FunctionProtos`.
2. `m1` is handed to the JIT. A fresh module `m2` is created for the next input.
3. `foo(2)` is generated in `m2`. `getFunction("foo")` doesn't find `foo` in `m2`.
4. It finds the saved prototype in `FunctionProtos` and calls `codegen()` on it, emitting a `declare` in `m2`.
5. The JIT resolves that `declare` to the already-compiled body in `m1`.

In IR, the two modules look like this:

```llvm
; m1 — compiled when the user typed: def foo(x): return x * x
define double @foo(double %x) {
entry:
  %multmp = fmul double %x, %x
  ret double %multmp
}
```

```llvm
; m2 — compiled when the user typed: foo(2)
declare double @foo(double)    ; no body here — resolved by the JIT to @foo in m1

define double @__anon_expr() {
entry:
  %calltmp = call double @foo(double 2.000000e+00)
  ret double %calltmp
}
```

`extern def` declarations follow the same pattern. After codegen, the prototype is saved:

```cpp
// In HandleExtern — save the prototype so getFunction() can re-emit it later.
FunctionProtos[ProtoAST->getName()] = move(ProtoAST);
```

And every function definition registers its prototype (name and signature) before codegenning the body:

```cpp
// In FunctionAST::codegen — register prototype before resolving the Function*.
auto &P = *Proto;
FunctionProtos[Proto->getName()] = move(Proto);
Function *TheFunction = getFunction(P.getName());
```

## The Runtime Library

This chapter adds two built-in functions callable from Pyxc via `extern def`:

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

The `DLLEXPORT` attribute places these symbols in pyxc's symbol table, making them visible to the LLVM JIT's function resolver which can then execute them. So you can just say:

```pyxc
extern def putchard(x)

# It is now ready to use. The JIT will find the function definition in the pyxc executable process.
putchard(65) # prints an 'A' on the screen
```

This also means you could move the runtime library into a separate `lib.cpp`, compile it into the `pyxc` binary, and have one clean place for all built-in functions. I'll do that in a later chapter.

`DLLEXPORT` doesn't do anything on macOS and Linux since in these cases symbols are exported by default. On Windows, symbols are not exported from executables by default, so the macro expands to `__declspec(dllexport)` to make them visible to the JIT.

## Command-Line Parsing

I also add a `-O` flag to control the optimization level. Rather than parsing `argv` by hand, I use LLVM's `CommandLine` library:

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

`-O1`, `-O2`, and `-O3` all do the same thing for now — they enable the same three passes. The infrastructure is in place, but the passes aren't yet wired to LLVM's predefined optimization presets. I'll connect them in a later chapter, once I'm done using this simplified pipeline for learning.

## Build and Run

```bash
cd code/chapter-07
cmake -S . -B build && cmake --build build
./build/pyxc
```

## Try It

### `extern` resolves from the process

<!-- code-merge:start -->
```pyxc
ready> extern def sin(x)
```

```bash
Parsed an extern.
```

```llvm
declare double @sin(double)
```

```pyxc
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

Since `sin` is declared `extern`, the JIT looks it up in the functions already loaded into the `pyxc` process — where the C standard library's `sin` is already present. 

Notice that the IR returns `sin(1)` as a constant (`0x3FEAED548F090CEE` — the hex encoding of `≈ 0.841471`) rather than using the call result. `InstCombinePass` folded the value, but the `call` itself is still kept because my `declare` does not mark `sin` as side-effect-free (`readnone`/`readonly`). So this IR is conservative: it preserves the call, but the returned value is constant-folded. This is the same limitation noted in [Known Limitations](#known-limitations).

### The Pythagorean identity

<!-- code-merge:start -->
```pyxc
ready> extern def cos(x)
```

```bash
Parsed an extern.
```

```llvm
declare double @cos(double)
```

```pyxc
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

```pyxc
ready> foo(4)
```

```bash
Evaluated to 1.000000
```
<!-- code-merge:end -->

`sin²(x) + cos²(x) = 1` for any x — the Pythagorean identity. The JIT compiled `foo`, resolved the native `sin` and `cos`, and executed the whole thing. The call duplication (two calls to `sin`, two to `cos`) will not be eliminated due to [Known Limitations](#known-limitations). 

### The optimizer at work

<!-- code-merge:start -->
```pyxc
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

```pyxc
ready> foo(2)
```

```bash
Evaluated to 25.000000
```
<!-- code-merge:end -->

Six source operations, two IR instructions. `IRBuilder` folded `1+2` to `3.0` at construction time. `ReassociatePass` and `GVNPass` recognized that both factors of the multiply are `x+3` and eliminated the duplicate `fadd`, leaving `%addtmp * %addtmp`. `(3+2)*(2+3) = 5*5 = 25`. Correct.

### The runtime library

<!-- code-merge:start -->
```pyxc
ready> extern def printd(x)
```

```bash
Parsed an extern.
```

```llvm
declare double @printd(double)
```

```pyxc
ready> printd(42)
```

```bash
42.000000
Evaluated to 0.000000
```
<!-- code-merge:end -->

`42.000000` is printed by `printd`'s own `fprintf`. `Evaluated to 0.000000` is the JIT printing `printd`'s return value (always `0.0`) after executing the `__anon_expr` wrapper.

`putchard` works the same way — it prints a single ASCII character by code point:

<!-- code-merge:start -->
```pyxc
ready> extern def putchard(x)
```

```bash
Parsed an extern.
```

```llvm
declare double @putchard(double)
```

```pyxc
ready> putchard(65)
```

```bash
Parsed a top-level expression.
```

```llvm
define double @__anon_expr() {
entry:
  %calltmp = call double @putchard(double 6.500000e+01)
  ret double %calltmp
}
```

```bash
AEvaluated to 0.000000
```
<!-- code-merge:end -->

ASCII 65 is `'A'`. It prints directly to stderr with no newline, so `A` and `Evaluated to 0.000000` run together on the same line. Not the cleanest output — I'll learn how to do more with this in later chapters.

## Known Limitations

- **Duplicate extern calls not eliminated.** `sin(x)*sin(x)` calls `sin` twice. GVN cannot merge calls to extern functions without knowing they're pure — i.e. that they always return the same value for the same input and have no side effects. LLVM has a way to express this: function attributes like `readnone` on the declaration tell the optimizer it's safe to deduplicate or eliminate the call. But Pyxc currently has no way for the programmer to declare a function as pure, and no way to infer it automatically. Until that infrastructure exists, the optimizer has to assume every `extern` call might do something observable and leaves them all in.

## What's Next

[Chapter 8](chapter-08.md) adds file input mode and a `-v` flag for IR inspection — `pyxc script.pyxc` runs a source file through the same JIT pipeline as the REPL, and `pyxc script.pyxc -v` prints the generated IR. That's the foundation real programs need.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.
