---
description: "Add ORC JIT and an optimization pass pipeline: top-level expressions now execute immediately and functions come out smaller. Also adds the extern keyword for calling real C library functions."
---
# 8. pyxc: JIT and Optimization

## What I Am Building

In [Chapter 7](chapter-07.md), I generated LLVM IR but did not execute it. For example:

<!-- code-merge:start -->
```pyxc
ready> def foo(x): (1+2+x)*(x+(1+2))
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

I can see the IR for `foo(2)`, but I do not get its result. The IR for `foo` also calculates `x + 3` twice.

In this chapter, I add a JIT so I can execute `foo(2)` and print its result:

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

I also add an optimization pipeline that removes the repeated calculation from `foo`:

<!-- code-merge:start -->
```pyxc
ready> def foo(x): (1+2+x)*(x+(1+2))
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

The generated function now contains two instructions instead of three. LLVM's optimizer rewrites both factors into the same value, so I only need one `fadd`.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-08
```

## Grammar

`extern` needs a rule of its own. It reuses `function-signature` but never reaches a body:

`code/chapter-08/pyxc.ebnf`

```grammardiff
 program                           = [ end-of-lines ]
                                     [ top-level-item
                                       { end-of-lines top-level-item } ]
                                     [ end-of-lines ] ;
 end-of-lines                      = end-of-line { end-of-line } ;
 top-level-item                    = function-definition
+                                    | external
                                     | top-level-expression ;
 function-definition               = "def" function-signature ":"
                                     [ end-of-lines ] expression ;
+external                          = "extern" "def" function-signature ;
 top-level-expression              = expression ;
 function-signature                = name "(" [ parameters ] ")" ;
 parameters                        = parameter { "," parameter } ;
 parameter                         = name ;
 expression                        = comparison ;
 comparison                        = sum { "<" sum } ;
 sum                               = term { ("+" | "-") term } ;
 term                              = factor { ("*" | "/" | "%") factor } ;
 factor                            = "-" factor | primary ;
 primary                           = name-expression
                                     | number-expression
                                     | parenthesized-expression ;
 name-expression                   = name
                                     | call-expression ;
 call-expression                   = name "(" [ arguments ] ")" ;
 arguments                         = expression { "," expression } ;
 number-expression                 = number ;
 parenthesized-expression          = "(" expression ")" ;
 name                              = (letter | "_")
                                     { letter | digit | "_" } ;
 number                            = digit { digit } [ "." { digit } ]
                                     | "." digit { digit } ;
 letter                            = "A".."Z" | "a".."z" ;
 digit                             = "0".."9" ;
 end-of-line                       = "\r\n" | "\r" | "\n" ;
 comment                           = "#" { comment-character } ;
 comment-character                 = ? any character except "\r" and "\n" ? ;
 whitespace                        = " " | "\t" | "\v" | "\f" ;
```

## Creating the ORC JIT

ORC stands for **On-Request Compilation**. LLVM provides it as a framework for building JIT compilers. ([ORCv2 docs](https://llvm.org/docs/ORCv2.html))

ORC accepts LLVM modules and makes their symbols available to compiled code. When I look up a symbol such as `__anon_expr`, ORC gives me the address of its machine code. I wrap ORC's building blocks in a small `PyxcJIT` class, defined in `code/include/PyxcJIT.h` and shared by every chapter from here on, so I don't repeat the same JIT boilerplate in each chapter's `pyxc.cpp`.

I create one `PyxcJIT` instance in `main()`. Before I create it, LLVM requires me to initialize support for the host machine:

```cpp
static unique_ptr<PyxcJIT> TheJIT;
static ExitOnError ExitOnErr;

// I initialize LLVM's backend for the host machine: its instruction set and
// assembler, so the JIT can compile and link for the current CPU.
InitializeNativeTarget();
InitializeNativeTargetAsmPrinter();

TheJIT = ExitOnErr(PyxcJIT::Create());
InitializeModuleAndManagers();
```

LLVM returns recoverable errors from many JIT operations. I use `ExitOnErr` to unwrap a successful result or stop the program when one of those operations fails.

## The Optimization Pipeline

LLVM organizes optimizations into passes. Some passes inspect a complete module; others inspect one function. I use function passes and run them immediately after I generate each function.

I store my passes in a [`FunctionPassManager`](https://llvm.org/docs/NewPassManager.html). When I run the manager, LLVM applies the passes in order and updates the function. I start with three passes:

| Pass | What it does |
|---|---|
| `InstCombinePass` | Simplifies individual instructions: `x * 1 → x`, `x + 0 → x`, and similar peephole rewrites |
| `ReassociatePass` | Reorders additions and multiplications so constants end up together: `(x+2)+3` becomes `x+(2+3)`, which then collapses to `x+5` |
| `GVNPass` | Finds places where the same value is computed twice and removes the duplicate. This is what eliminates the second `fadd` in `foo` |

These passes do not fold `1 + 2` into `3`. `IRBuilder` performs that constant folding while I construct the IR.

## Initializing the Module and Managers

I extend `InitializeModuleAndManagers()` from [Chapter 7](chapter-07.md). I call it at startup and again each time I hand a module to the JIT. It now creates the module, the builder, and the pass infrastructure.

### Creating a Fresh Module

I create a new context, module, and builder. I also copy the JIT's target data layout into the module:

```cpp
TheContext = std::make_unique<LLVMContext>();
TheModule = std::make_unique<Module>("PyxcJIT", *TheContext);
TheModule->setDataLayout(TheJIT->getDataLayout());
Builder = std::make_unique<IRBuilder<>>(*TheContext);
```

LLVM requires the module's data layout to match the JIT target. It describes details such as pointer widths and type alignment.

### Creating the Analysis Managers

LLVM's pass framework requires analysis managers for loops, functions, call graphs, and modules. I create and register all four, even though my current pipeline only runs function passes:

```cpp
static unique_ptr<LoopAnalysisManager> TheLAM;
static unique_ptr<FunctionAnalysisManager> TheFAM;
static unique_ptr<CGSCCAnalysisManager> TheCGAM;
static unique_ptr<ModuleAnalysisManager> TheMAM;

TheLAM = make_unique<LoopAnalysisManager>();
TheFAM = make_unique<FunctionAnalysisManager>();
TheCGAM = make_unique<CGSCCAnalysisManager>();
TheMAM = make_unique<ModuleAnalysisManager>();

PassBuilder PB;
PB.registerModuleAnalyses(*TheMAM);
PB.registerCGSCCAnalyses(*TheCGAM);
PB.registerFunctionAnalyses(*TheFAM);
PB.registerLoopAnalyses(*TheLAM);
// I let a pass request analysis results managed at another level.
PB.crossRegisterProxies(*TheLAM, *TheFAM, *TheCGAM, *TheMAM);
```

### Building the Function Pipeline

I add the optimization passes to `TheFPM` when optimization is enabled:

```cpp
static unique_ptr<FunctionPassManager> TheFPM;

TheFPM = make_unique<FunctionPassManager>();

if (OptLevel != 0) {
  TheFPM->addPass(InstCombinePass());
  TheFPM->addPass(ReassociatePass());
  TheFPM->addPass(GVNPass());
}
```

I define `OptLevel` from the `-O` command-line option later in this chapter. At `-O0`, I leave the pipeline empty.

### Running the Pipeline

After I generate and verify a function, I run the pipeline:

```cpp
// In FunctionDefinitionNode::codegen(), after verifyFunction():
TheFPM->run(*TheFunction, *TheFAM);
```

This optimizes each function before I print or compile it.

## Executing Top-Level Expressions

For a top-level expression, I generate and optimize `__anon_expr` as before. I then hand its module to the JIT and execute the compiled function:

```cpp
if (auto *FunctionIR = FunctionDefinition->codegen()) {
  FunctionIR->print(errs());

  // ResourceTracker scopes the JIT memory for this expression so I can
  // free it precisely after the call, without affecting other symbols.
  auto RT = TheJIT->getMainJITDylib().createResourceTracker();

  // Transfer ownership of the module to the JIT; reinitialise for next input.
  auto TSM = ThreadSafeModule(std::move(TheModule), std::move(TheContext));
  ExitOnErr(TheJIT->addModule(std::move(TSM), RT));
  InitializeModuleAndManagers();

  // Locate the compiled function in the JIT's symbol table.
  auto ExprSymbol = ExitOnErr(TheJIT->lookup("__anon_expr"));

  // Cast the symbol address to a callable function pointer and invoke it.
  double (*FP)() = ExprSymbol.toPtr<double (*)()>();
  double result = FP();
  fprintf(stderr, "Evaluated to %f\n", result);

  // Release the compiled code and JIT memory for this expression.
  ExitOnErr(RT->remove());
}
```

`lookup("__anon_expr")` asks the JIT for the compiled symbol. I use `toPtr<double (*)()>` to treat its address as a C function that takes no arguments and returns a `double`. Calling `FP()` executes that machine code.

I create a `ResourceTracker` before adding the module. After the call, `RT->remove()` frees the object file and executable memory associated with `__anon_expr`. This replaces the `eraseFromParent()` call from Chapter 7: I now compile and execute the expression before I remove it.

> LLVM uses `Expected<T>` for operations that may fail. I pass those results to `ExitOnErr`, which unwraps a successful value or terminates with the error.

For named functions, I add the module without this temporary tracker, so their compiled code remains available for the rest of the session.

## Calling Functions I Didn't Write

Now that I can execute code, I want to call functions whose implementations live outside pyxc source, such as the C library's `sin` and `cos`. A `def` always includes a body, so I add `extern` for a signature without a body.

A new token:

```cpp
enum Token {
  // ...
  tok_def = -4,
  tok_extern = -5,
  // ...
};
```

I add both keywords to the keyword table:

```cpp
static map<string, Token> Keywords = {
    {"def", tok_def},
    {"extern", tok_extern},
};
```

`external` reuses the same `function-signature` rule `def` uses. I reuse `ParseFunctionSignature()` after consuming `extern def`:

```cpp
/// external
///   = "extern" "def" function-signature
static unique_ptr<FunctionSignatureNode> ParseExtern() {
  getNextToken(); // eat extern.
  if (CurrentToken != tok_def)
    return LogErrorSignature("Expected `def` after extern.");
  getNextToken(); // eat def
  return ParseFunctionSignature();
}
```

I stop after the signature instead of parsing a colon and body.

I do not need a new code-generation method. `FunctionSignatureNode::codegen()` already creates a `Function*` with no body, which LLVM prints as a declaration:

```llvm
declare double @sin(double)
```

Because I do not call `FunctionDefinitionNode::codegen()`, I never add a body.

I connect parsing and code generation in `HandleExtern()`:

```cpp
static void HandleExtern() {
  auto Signature = ParseExtern();

  if (!Signature || (CurrentToken != tok_eol && CurrentToken != tok_eof)) {
    if (Signature)
      LogErrorExpression(("Unexpected " + FormatTokenForMessage(CurrentToken)).c_str());
    SynchronizeToLineBoundary();
    return;
  }

  // Reject conflicting redeclarations: in Pyxc, function identity is just
  // name + arity, since all parameter and return types are double.
  auto Existing = FunctionSignatures.find(Signature->getName());
  if (Existing != FunctionSignatures.end() &&
      Existing->second->getNumParameters() != Signature->getNumParameters()) {
    LogErrorExpression((string("Conflicting extern declaration for '") +
              Signature->getName() + "'")
                 .c_str());
    SynchronizeToLineBoundary();
    return;
  }

  if (auto *FunctionIR = Signature->codegen()) {
    Log("Parsed an extern.\n");
    FunctionIR->print(errs());
    // Save the function signature so getFunction() can re-emit it in future modules.
    FunctionSignatures[Signature->getName()] = std::move(Signature);
  }
}
```

I save the signature in `FunctionSignatures` so I can use it again after I replace the current module. I explain that registry shortly. The immediate result is:

```pyxc
ready> extern def sin(x)
Parsed an extern.
declare double @sin(double)
```

I also dispatch `tok_extern` from `MainLoop()`:

```cpp
switch (CurrentToken) {
case tok_def:
  HandleFunctionDefinition();
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

When I parse `foo(2)`, I wrap it in a zero-argument function named `__anon_expr`. I compile it, call it, and then free it. ORC tracks resources at the module level, so I must keep this temporary function separate from named functions that need to remain available.

For simplicity, I place every external declaration, function definition, and top-level expression in a fresh module. Removing the module for `__anon_expr` therefore removes only that temporary function.

I could group named functions into larger modules while keeping anonymous expressions separate, but I defer that module-management policy.

I still reject a second definition of the same function. Supporting redefinition in the REPL would require me to replace the previously compiled symbol safely, which I leave for later.

Because the JIT takes ownership of each module and its context, I create replacements immediately after every transfer:

```cpp
// Hand the module to the JIT.
ExitOnErr(TheJIT->addModule(ThreadSafeModule(move(TheModule), move(TheContext))));

// Start fresh for the next input.
InitializeModuleAndManagers();
```

ORC requires me to transfer the `Module` together with its `LLVMContext`, so I package them in a `ThreadSafeModule`.

## The Cross-Module Function Lookup Problem

In Chapter 7, I found a called function with `TheModule->getFunction(Callee)`. That only searches the current module. After I hand `foo`'s module to the JIT and create a new module, that lookup can no longer find `foo`.

I solve this by keeping a persistent registry of `FunctionSignatureNode` objects. My `getFunction()` helper first searches the current module and then uses the saved signature to recreate a declaration when necessary:

```cpp
static map<string, unique_ptr<FunctionSignatureNode>> FunctionSignatures;

Function *getFunction(const std::string &Name) {
  // I first search the current module.
  if (auto *F = TheModule->getFunction(Name))
    return F;

  // Otherwise, I recreate a declaration from the saved signature.
  auto FI = FunctionSignatures.find(Name);
  if (FI != FunctionSignatures.end())
    return FI->second->codegen();

  return nullptr;
}
```

I also update `CallExpressionNode::codegen()` from Chapter 7 to call `getFunction(Callee)` instead of `TheModule->getFunction(Callee)`, so a call to a function from an earlier module resolves the same way:

```cpp
Value *CallExpressionNode::codegen() {
  Function *CalleeF = getFunction(Callee);
  if (!CalleeF)
    return LogErrorV("Unknown function referenced");
  // ...
```

The sequence is:

1. I compile `def foo` into module `m1` and save its signature.
2. I hand `m1` to the JIT and create module `m2`.
3. While generating `foo(2)` in `m2`, I cannot find `foo` in the current module.
4. I use the saved signature to emit `declare double @foo(double)` in `m2`.
5. When ORC links `m2`, it resolves that declaration to the body already compiled from `m1`.

In IR, the two modules look like this:

```llvm
; m1 — compiled from: def foo(x): x * x
define double @foo(double %x) {
entry:
  %multmp = fmul double %x, %x
  ret double %multmp
}
```

```llvm
; m2 — compiled from: foo(2)
declare double @foo(double) ; ORC resolves this to @foo in m1

define double @__anon_expr() {
entry:
  %calltmp = call double @foo(double 2.000000e+00)
  ret double %calltmp
}
```

I save an `extern def` signature after generating its declaration:

```cpp
// Save the function signature so getFunction() can re-emit it in future modules.
FunctionSignatures[Signature->getName()] = std::move(Signature);
```

I also save the signature of every function definition before generating its body:

```cpp
// Step 1: register the function signature and resolve the Function*.
auto &P = *Signature;
FunctionSignatures[Signature->getName()] = std::move(Signature);

// Step 1: reuse an existing `extern` declaration if one exists.
Function *TheFunction = getFunction(P.getName());
```

## The Runtime Library

I add two C++ functions that pyxc can call through `extern def`:

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

I give the functions C linkage so their symbol names remain `putchard` and `printd`. `DLLEXPORT` makes those symbols visible from the executable on Windows. The JIT can then resolve an external declaration against them:

```pyxc
extern def putchard(x)

# The JIT resolves this call to the function in the pyxc executable.
putchard(65)
```

I will move these functions into a separate runtime-library file in a later chapter.

On Windows, executable symbols are not exported by default, so `DLLEXPORT` expands to `__declspec(dllexport)`. The macro is empty on macOS and Linux in this project.

## Command-Line Parsing

I add a `-O` option and use LLVM's command-line library to parse it:

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

I use `cl::Prefix` to accept `-O2` instead of requiring `-O=2`. LLVM also uses the description to generate `--help` output.

At `-O0`, I leave the pass pipeline empty so I can inspect the IR before optimization.

For now, `-O1`, `-O2`, and `-O3` all enable the same three passes. I do not connect these values to LLVM's full optimization presets yet.

## Build and Run

```bash
cd code/chapter-08
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

Because I declare `sin` as external, ORC searches the symbols available to the `pyxc` process and resolves the C library implementation.

The IR returns the constant encoding of approximately `0.841471`, but it still contains the call. My declaration does not mark `sin` as free of side effects, so LLVM must preserve that call even when it can fold the returned value.

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
ready> def foo(x): sin(x)*sin(x)+cos(x)*cos(x)
```

```bash
Parsed a function definition.
```

```llvm
define double @foo(double %x) {
entry:
  %calltmp = call double @sin(double %x)
  %calltmp1 = call double @sin(double %x)
  %multmp = fmul double %calltmp, %calltmp1
  %calltmp2 = call double @cos(double %x)
  %calltmp3 = call double @cos(double %x)
  %multmp4 = fmul double %calltmp2, %calltmp3
  %addtmp = fadd double %multmp, %multmp4
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

The expression uses the identity `sin²(x) + cos²(x) = 1`. I compile `foo`, ORC resolves the native `sin` and `cos` functions, and I execute the result. LLVM retains both calls to each external function because their declarations do not describe them as pure.

### The optimizer at work

<!-- code-merge:start -->
```pyxc
ready> def foo(x): (1+2+x)*(x+(1+2))
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

`IRBuilder` folds each `1 + 2` to `3.0` while I generate the IR. `ReassociatePass` puts the remaining additions into a common form, and `GVNPass` removes the repeated `fadd`. I am left with two IR instructions.

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

The runtime function prints `42.000000`. It returns `0.0`, which I then print as the result of `__anon_expr`.

I can use `putchard` to print one ASCII character from its numeric code:

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

ASCII code 65 represents `A`. `putchard` does not print a newline, so the character and the evaluation message appear on the same line.

## Known Limitations

- **I do not eliminate duplicate external calls.** LLVM cannot merge two calls to an external function unless its declaration proves that the function has no relevant side effects. pyxc does not add those function attributes yet, so LLVM preserves every call.

## What's Next

[Chapter 9](chapter-09.md) adds file input mode, so whole source files run through the same pipeline as the REPL.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.
