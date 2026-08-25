---
description: "Add ORC JIT and an optimization pass pipeline: top-level expressions now execute immediately and functions come out smaller. Also adds the extern keyword for calling real C library functions."
---
# 8. pyxc: JIT and Optimization

## What I Am Building

So far my compiler has been an excellent typist: it can turn `foo(2)` into a perfectly formed page of LLVM IR and then just sit there, IR in hand, doing nothing else with it, which is a bit like ordering dinner and receiving a very detailed recipe instead. This chapter finally executes the thing. While I'm in there I also notice my compiler has been computing `1 + 2` twice for no reason, so I fix that too.

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
*...
*end-of-lines                      = end-of-line { end-of-line } ;
*top-level-item                    = function-definition
+                                    | external
*                                    | top-level-expression ;
*function-definition               = "def" function-signature ":"
*                                    [ end-of-lines ] expression ;
+external                          = "extern" "def" function-signature ;
*top-level-expression              = expression ;
*function-signature                = name "(" [ parameters ] ")" ;
*...
```

`extern` declares a function signature with no body, for calling into code that isn't written in pyxc, such as the C library's `sin` and `cos`. I cover parsing and resolving `extern` declarations in [Calling Functions I Didn't Write](#calling-functions-i-didnt-write) later in this chapter. The JIT and optimization pipeline come first, since resolving an `extern` declaration against real code is the JIT's job.

## Creating the ORC JIT

ORC stands for **On-Request Compilation**. LLVM provides it as a framework for building JIT compilers. ([ORCv2 docs](https://llvm.org/docs/ORCv2.html))

ORC accepts LLVM modules and makes their symbols available to compiled code. When I look up a symbol such as `__anon_expr`, ORC gives me the address of its machine code. I wrap ORC's building blocks in a small `PyxcJIT` class, defined in `code/include/PyxcJIT.h` and shared by every chapter from here on, so I don't repeat the same JIT boilerplate in each chapter's `pyxc.cpp`. I include it before the LLVM headers, at the very top of the file:

```cppdiff
+#include "../include/PyxcJIT.h"
*#include "llvm/ADT/APFloat.h"
*#include "llvm/IR/BasicBlock.h"
*...
```

I create one `PyxcJIT` instance in `main()`. I hold it, and the helper LLVM uses to unwrap recoverable errors, as globals. `JIT` goes right after `NamedValues`, in the same block of file-scope state; `ExitOnErr` goes at the very end of that block, after the pass-manager globals I add later in this chapter:

```cppdiff
*static unique_ptr<LLVMContext> TheContext;
*static unique_ptr<Module> TheModule;
*static unique_ptr<IRBuilder<>> TheBuilder;
*static map<string, Value *> NamedValues;
+static unique_ptr<PyxcJIT> JIT;
*...
+static ExitOnError ExitOnErr;
```

LLVM returns recoverable errors from many JIT operations. I use `ExitOnErr` to unwrap a successful result or stop the program when one of those operations fails.

Before I create the JIT, LLVM requires me to initialize support for the host machine:

```cpp
#include "llvm/Support/TargetSelect.h"

int main(int argc, const char **argv) {
  ...  

  // Initialise LLVM's backend for the host machine. These three calls
  // together register the native target's instruction set, assembler, and
  // disassembler so the JIT can compile and link for the current CPU.
  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();

  ...

}
```

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

I create a new context, module, and builder. I also copy the JIT's target data layout into the module. `InitializeModuleAndManagers()` already existed in [Chapter 7](chapter-07.md) with the first, third, and fourth of these lines; I add the `setDataLayout()` call:

```cppdiff
*static void InitializeModuleAndManagers() {
*  TheContext = std::make_unique<LLVMContext>();
*  TheModule = std::make_unique<Module>("PyxcJIT", *TheContext);
+  TheModule->setDataLayout(JIT->getDataLayout());
*  TheBuilder = std::make_unique<IRBuilder<>>(*TheContext);
*  ...
```

LLVM requires the module's data layout to match the JIT target. It describes details such as pointer widths and type alignment.

### Creating the Analysis Managers

LLVM's pass framework requires analysis managers for loops, functions, call graphs, and modules. I add file-scope globals for all four, plus the function pass manager itself, right after `JIT` in that same block:

```cppdiff
+#include "llvm/Passes/PassBuilder.h"
+#include "llvm/Transforms/InstCombine/InstCombine.h"
+#include "llvm/Transforms/Scalar/GVN.h"
+#include "llvm/Transforms/Scalar/Reassociate.h"
*
*static unique_ptr<PyxcJIT> JIT;
+// New Analysis passes
+static unique_ptr<FunctionPassManager> FunctionPasses;
+static unique_ptr<LoopAnalysisManager> LoopAnalyses;
+static unique_ptr<FunctionAnalysisManager> FunctionAnalyses;
+static unique_ptr<CGSCCAnalysisManager> CallGraphAnalyses;
+static unique_ptr<ModuleAnalysisManager> ModuleAnalyses;
*...
*static ExitOnError ExitOnErr;
```

Back in `InitializeModuleAndManagers()`, right after building `TheBuilder`, I create and register all four, even though my current pipeline only runs function passes:

```cppdiff
*  ...
*  TheBuilder = std::make_unique<IRBuilder<>>(*TheContext);
*
+  FunctionPasses = std::make_unique<FunctionPassManager>();
+  LoopAnalyses = std::make_unique<LoopAnalysisManager>();
+  FunctionAnalyses = std::make_unique<FunctionAnalysisManager>();
+  CallGraphAnalyses = std::make_unique<CGSCCAnalysisManager>();
+  ModuleAnalyses = std::make_unique<ModuleAnalysisManager>();
+
+  PassBuilder PB;
+  PB.registerModuleAnalyses(*ModuleAnalyses);
+  PB.registerCGSCCAnalyses(*CallGraphAnalyses);
+  PB.registerFunctionAnalyses(*FunctionAnalyses);
+  PB.registerLoopAnalyses(*LoopAnalyses);
+  // I let a pass request analysis results managed at another level.
+  PB.crossRegisterProxies(*LoopAnalyses, *FunctionAnalyses, *CallGraphAnalyses, *ModuleAnalyses);
*  ...
```

### Building the Function Pipeline

I add the optimization passes to `FunctionPasses`, closing out `InitializeModuleAndManagers()`:

```cppdiff
*  ...
+  FunctionPasses->addPass(InstCombinePass());
+  FunctionPasses->addPass(ReassociatePass());
+  FunctionPasses->addPass(GVNPass());
*}
```

### Running the Pipeline

After I generate and verify a function, I run the pipeline:

```cppdiff
*Function *FunctionDefinitionNode::codegen() {
*  ...
*  if (Value *RetVal = Body->codegen()) {
*    TheBuilder->CreateRet(RetVal);
*    verifyFunction(*TheFunction);
*
+    // Run the optimisation pipeline: InstCombine, Reassociate, GVN,
+    // SimplifyCFG.
+    FunctionPasses->run(*TheFunction, *FunctionAnalyses);
*    return TheFunction;
*  }
*  ...
*}
```

This optimizes each function before I print or compile it.

## Executing Top-Level Expressions

Every `Handle*` function below confirms what it parsed through one small helper, `Log()`, instead of calling `fprintf(stderr, ...)` directly as it did in Chapter 7. That gives the pattern one place to change later, if I ever want it to go somewhere other than `stderr`:

```cpp
void Log(const string &message) { fprintf(stderr, "%s", message.c_str()); }

void PrintEvaluationResult(double Result) {
  fprintf(stdout, "Evaluated to %f\n", Result);
}
```

`Log()` is for compiler progress on `stderr`. `PrintEvaluationResult()` keeps
the REPL's user-facing result on `stdout` and gives that formatting one clear
home.

For a top-level expression, I generate and optimize `__anon_expr` as before. I then hand its module to the JIT and execute the compiled function:

```cppdiff
*static void HandleTopLevelExpression() {
*  ...
*
*  auto *FunctionIR = FunctionDefinition->codegen();
*  if (!FunctionIR)
*    return;
*
-  fprintf(stderr, "Parsed a top-level expression.\n");
-  FunctionIR->print(errs());
-  fprintf(stderr, "\n");
-
-  // Erase after printing — anonymous expressions don't belong in the final
-  // module dump.
-  FunctionIR->eraseFromParent();
*
+  Log("Parsed a top-level expression.\n");
+  FunctionIR->print(errs());
+
+  // ResourceTracker scopes the JIT memory for this expression so we can
+  // free it precisely after the call, without affecting other symbols.
+  auto RT = JIT->getMainJITDylib().createResourceTracker();
+
+  // Transfer ownership of the module to the JIT; reinitialise for next input.
+  auto TSM = ThreadSafeModule(std::move(TheModule), std::move(TheContext));
+  ExitOnErr(JIT->addModule(std::move(TSM), RT));
+  InitializeModuleAndManagers();
+
+  // Locate the compiled function in the JIT's symbol table.
+  auto ExprSymbol = ExitOnErr(JIT->lookup(AnonymousExpressionFunctionName));
+
+  // Cast the symbol address to a callable function pointer and invoke it.
+  double (*FP)() = ExprSymbol.toPtr<double (*)()>();
+  double result = FP();
+  PrintEvaluationResult(result);
+
+  // Release the compiled code and JIT memory for this expression.
+  ExitOnErr(RT->remove());
*}
```

`JIT->lookup(AnonymousExpressionFunctionName)` asks the JIT for the compiled
symbol named `__anon_expr`. I use `toPtr<double (*)()>` to treat its address as
a C function that takes no arguments and returns a `double`. Calling `FP()`
executes that machine code.

I now separate successful output from compiler diagnostics. I write the value produced by the REPL and anything printed by pyxc code to `stdout`. I keep prompts, errors, parse messages, and IR dumps on `stderr`. Before I print the next prompt, I flush `stdout` so output from the expression appears first.

I create a `ResourceTracker` before adding the module. After the call, `RT->remove()` frees the object file and executable memory associated with `__anon_expr`. This replaces the `eraseFromParent()` call from Chapter 7: I now compile and execute the expression before I remove it.

`ExitOnErr` and `RT` solve two unrelated problems, even though they sit next to each other on the page. `RT` is a handle: I create it before `addModule`, hand it to `addModule` so ORC associates `__anon_expr`'s compiled code with it, then later call `RT->remove()` to free that code once I'm done with it. It has nothing to do with error handling.

`ExitOnErr` is purely about error handling. Many LLVM APIs, including `addModule`, `RT->remove()`, and `JIT->lookup()`, don't throw exceptions or return null on failure; they return an `Error` (or `Expected<T>` if there's also a value to hand back on success) that the caller must check. `ExitOnErr` is a small callable I keep as a global: I pass it the `Error` or `Expected<T>` a call produced, and it does one of two things. If that value represents failure, it prints the error and terminates the process right there. If it represents success, it unwraps it and lets execution continue: for a plain `Error` there's no payload, so this is just "check and discard"; for `Expected<T>`, like `JIT->lookup()` returns, it hands back the wrapped value.

So `ExitOnErr(JIT->addModule(std::move(TSM), RT))` isn't releasing anything through `RT`. `addModule` returns `Error`, a pass/fail signal with no payload; `ExitOnErr` checks that signal and crashes with a message if it's a failure, or does nothing further if it's a success. `ExitOnErr(RT->remove())` a few lines later is the same pattern applied to a different call: `RT->remove()` also returns `Error`, so it also gets checked and unwrapped the same way, but the two calls are doing unrelated jobs, one adding a module, the other freeing one.

For named functions, I add the module without this temporary tracker, so their compiled code remains available for the rest of the session.

## One Module per Compilation Unit

`foo` and `bar` need to stay compiled and callable for the rest of the session. `__anon_expr`, the wrapper I generate for a bare top-level expression like `foo(1) + bar(2)`, needs the opposite: it runs exactly once and then has no reason to exist. ORC tracks resources at the level of a whole module, so I can't free just one function out of a module I've already handed over; I can only free (or keep) an entire module. That's the reason every `def`, every `extern`, and every top-level expression each gets its own fresh module: it lets me remove the module built for `__anon_expr` without touching the modules `foo` and `bar` live in.

STAGE 1 — Steady state: foo and bar are already compiled and living in the JIT

```diagram
  JIT symbol table
  ┌────────────────┐  ┌────────────────┐
  │ foo (compiled) │  │ bar (compiled) │
  └────────────────┘  └────────────────┘
  (waiting for the next top-level input...)
```

STAGE 2 — A top-level expression arrives: foo(1) + bar(2)
I wrap it in a throwaway function and compile that too:

```diagram
  double __anon_expr() { return foo(1) + bar(2); }

  JIT symbol table
  ┌────────────────┐  ┌────────────────┐  ┌───────────────────────┐
  │ foo (compiled) │  │ bar (compiled) │  │ __anon_expr (compiled)│
  └────────────────┘  └────────────────┘  └───────────────────────┘
```

STAGE 3 — I look up and call __anon_expr(); it calls the other two
```diagram
                    ┌───────────────────────┐
                    │ __anon_expr (compiled)│
                    └───────────────────────┘
                         │              │
                    calls│              │calls
                         ▼              ▼
              ┌────────────────┐  ┌────────────────┐
              │ foo (compiled) │  │ bar (compiled) │
              └────────────────┘  └────────────────┘
                    result=1            result=4
                         └──────┬───────┘
                                ▼
                   __anon_expr() returns 5
```

`foo` and `bar` resolve normally here because they're still in the JIT from Stage 1, even though `__anon_expr` lives in a completely different module compiled moments later. I cover exactly how that cross-module lookup works next. Once `__anon_expr()` returns, `RT->remove()` frees just its module, and the JIT is back to Stage 1: `foo` and `bar` untouched, ready for the next input.

This has a real cost: since each function compiles alone in its own module, LLVM never sees more than one function at a time. There's no cross-function optimization, no inlining `foo` into a caller, for as long as pyxc compiles this way. Grouping functions into shared modules isn't a small addition on top of this design either: it would mean not handing a module to the JIT the moment I finish compiling it, which conflicts with how a REPL works. I don't know what the next input will be, and once a module is transferred, ORC owns it; I can't add to it after the fact.

I still reject a second definition of the same function. Supporting redefinition in the REPL would require me to replace the previously compiled symbol safely, which I leave for later.

Because the JIT takes ownership of each module and its context, I create replacements immediately after every transfer, at the end of `HandleFunctionDefinition()`:

```cppdiff
*static void HandleFunctionDefinition() {
*  ...
*
*  auto *FunctionIR = FunctionDefinition->codegen();
*  if (!FunctionIR)
*    return;
*
-  fprintf(stderr, "Parsed a function definition.\n");
-  FunctionIR->print(errs());
-  fprintf(stderr, "\n");
*
+  Log("Parsed a function definition.\n");
+  FunctionIR->print(errs());
+  // Transfer the module to the JIT. TheModule is now invalid; reinitialise.
+  ExitOnErr(JIT->addModule(
+      ThreadSafeModule(std::move(TheModule), std::move(TheContext))));
+  InitializeModuleAndManagers();
*}
```

ORC requires me to transfer the `Module` together with its `LLVMContext`, so I package them in a `ThreadSafeModule`.

## The Cross-Module Function Lookup Problem

In Chapter 7, I found a called function with `TheModule->getFunction(Callee)`. That only searches the current module. After I hand `foo`'s module to the JIT and create a new module, that lookup can no longer find `foo`.

I solve this by keeping a persistent registry of `FunctionSignatureNode` objects. My `getFunction()` helper first searches the current module and then uses the saved signature to recreate a declaration when necessary:

```cpp
static map<string, unique_ptr<FunctionSignatureNode>> FunctionSignatures;

Function *getFunction(const string &Name) {
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

```cppdiff
*Value *CallExpressionNode::codegen() {
-  Function *CalleeF = TheModule->getFunction(Callee);
+  Function *CalleeF = getFunction(Callee);
*  if (!CalleeF)
*    return LogErrorValue("Unknown function: '" + Callee + "'");
*
*  ...
*
*  return TheBuilder->CreateCall(CalleeF, ArgsV, "calltmp");
*}
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

I also save the signature of every function definition before generating its body:

```cppdiff
*Function *FunctionDefinitionNode::codegen() {
*  const string FunctionName = Signature->getName();
*
-  // Step 1: I get an existing declaration or create a new one.
-  Function *TheFunction = TheModule->getFunction(FunctionName);
+  // Step 1: register the function signature and resolve the Function*.
+  FunctionSignatures[FunctionName] = std::move(Signature);
+
+  // Step 1: reuse an existing declaration if one exists.
+  Function *TheFunction = getFunction(FunctionName);
*
*  // Bail if the function is already fully defined — redefinition is an error.
*  if (TheFunction && !TheFunction->empty()) {
*    LogErrorExpression(
*        "Function '" + FunctionName + "' cannot be redefined");
*    return nullptr;
*  }
*
-  if (!TheFunction)
-    TheFunction = Signature->codegen();
-
*  if (!TheFunction)
*    return nullptr;
*
*  // Step 2: create the entry block and point the builder at it.
*  ...
*}
```

## Calling Functions I Didn't Write

Now that I can execute code, I want to call functions whose implementations live outside pyxc source, such as the C library's `sin` and `cos`. A `def` always includes a body, so I add `extern` for a signature without a body.

A new token:

```cppdiff
*enum Token {
*  ...
*  tok_def = -4,
+  tok_extern = -5,
*
*  // primary
-  tok_name = -5,
-  tok_number = -6,
+  tok_name = -6,
+  tok_number = -7,
*  ...
*};
```

I add both keywords to the keyword table:

```cppdiff
*static map<string, Token> Keywords = {
*    {"def", tok_def},
+    {"extern", tok_extern},
*};
```

`external` reuses the same `function-signature` rule `def` uses. I reuse `ParseFunctionSignature()` after consuming `extern def`:

```cpp
/// external
///   = "extern" "def" function-signature
static unique_ptr<FunctionSignatureNode> ParseExtern() {
  getNextToken(); // eat extern.
  if (CurrentToken != tok_def)
    return LogErrorSignature("Expected 'def' after 'extern'");
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

I add one method to `FunctionSignatureNode` itself, `getNumParameters()`, so I can compare arity between two signatures without exposing the raw `Parameters` vector:

```cppdiff
*class FunctionSignatureNode {
*  string Name;
*  vector<string> Parameters;
*
*public:
*  FunctionSignatureNode(const string &Name, vector<string> Parameters)
*      : Name(Name), Parameters(std::move(Parameters)) {}
*
*  const string &getName() const { return Name; }
+  size_t getNumParameters() const { return Parameters.size(); }
*  Function *codegen();
*};
```

I use it in `HandleExtern()`, next, to detect a redeclaration with a different arity. I connect parsing and code generation there:

```cpp
static void HandleExtern() {
  auto Signature = ParseExtern();

  if (!Signature) {
    DiscardRestOfLine();
    return;
  }

  if (CurrentToken != tok_eol && CurrentToken != tok_eof) {
    LogErrorExpression("Unexpected " + FormatTokenForMessage(CurrentToken));
    DiscardRestOfLine();
    return;
  }

  // Reject conflicting redeclarations: in pyxc, function identity is just
  // name + arity, since all parameter and return types are double.
  auto Existing = FunctionSignatures.find(Signature->getName());
  if (Existing != FunctionSignatures.end() &&
      Existing->second->getNumParameters() != Signature->getNumParameters()) {
    const size_t PreviousParameterCount =
        Existing->second->getNumParameters();
    const size_t NewParameterCount = Signature->getNumParameters();
    LogErrorExpression(
        "Conflicting declaration for function '" + Signature->getName() +
        "': previous declaration has " +
        to_string(PreviousParameterCount) +
        (PreviousParameterCount == 1 ? " parameter" : " parameters") +
        ", but this declaration has " + to_string(NewParameterCount) +
        (NewParameterCount == 1 ? " parameter" : " parameters"));
    DiscardRestOfLine();
    return;
  }

  auto *FunctionIR = Signature->codegen();
  if (!FunctionIR)
    return;

  Log("Parsed an extern.\n");
  FunctionIR->print(errs());
  // Save the function signature so getFunction() can re-emit it in future modules.
  FunctionSignatures[Signature->getName()] = std::move(Signature);
}
```

I save the signature in `FunctionSignatures`, the registry I introduced in [The Cross-Module Function Lookup Problem](#the-cross-module-function-lookup-problem), so I can use it again after I replace the current module. The immediate result is:

```pyxc
ready> extern def sin(x)
```
```text
Parsed an extern.
declare double @sin(double)
```

`PrintReplPrompt()` prints `ready> ` and replaces the raw `fprintf(stderr, "ready> ")` from Chapter 7:

```cpp
void PrintReplPrompt() {
  fflush(stdout);
  fprintf(stderr, "ready> ");
}
```

I flush `stdout` first so any output already produced by the program (through `printd` or `putchard`, once I add `extern` calls to those below) appears before the next prompt rather than getting buffered behind it.

I also dispatch `tok_extern` from `MainLoop()`, which now calls `PrintReplPrompt()` in its `tok_eol` case:

```cppdiff
*static void MainLoop() {
*  while (CurrentToken != tok_eof) {
*    switch (CurrentToken) {
*    case tok_error:
*      DiscardRestOfLine();
*      break;
*    case tok_eol:
-      // For a bare newline, I print a fresh prompt and read the next token.
-      fprintf(stderr, "ready> ");
+      // A bare newline: just print a fresh prompt and read the next token.
+      PrintReplPrompt();
*      getNextToken();
*      break;
*    case tok_def:
*      HandleFunctionDefinition();
*      break;
+    case tok_extern:
+      HandleExtern();
+      break;
*    default:
*      HandleTopLevelExpression();
*      break;
*    }
*  }
*}
```

## The Runtime Library

I add two C++ functions that pyxc can call through `extern def`. On Windows, executable symbols are not exported by default, so I define `DLLEXPORT` to expand to `__declspec(dllexport)`; the macro is empty on macOS and Linux, where `extern "C"` symbols are already visible:

```cpp
#ifdef _WIN32
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

extern "C" DLLEXPORT double putchard(double X) {
  fputc((char)X, stdout);
  return 0;
}

extern "C" DLLEXPORT double printd(double X) {
  fprintf(stdout, "%f\n", X);
  return 0;
}
```

I give the functions C linkage so their symbol names remain `putchard` and `printd`. The JIT can then resolve an external declaration against them:

```pyxc
extern def putchard(x)

# The JIT resolves this call to the function in the pyxc executable.
putchard(65)
```

I will move these functions into a separate runtime-library file in a later chapter.

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

I parse and validate the flag in `ProcessCommandLine()`, which `main()` now calls before doing anything else:

```cpp
int ProcessCommandLine(int argc, const char **argv) {
  cl::HideUnrelatedOptions(PyxcCategory);
  cl::ParseCommandLineOptions(argc, argv, "pyxc\n");

  if (OptLevel > 3) {
    fprintf(stderr, "Error: -O level must be 0, 1, 2, or 3\n");
    return -1;
  }

  return 0;
}
```

`cl::HideUnrelatedOptions` hides LLVM's own long list of built-in flags from `--help`, showing only the options I registered under `PyxcCategory`. `cl::ParseCommandLineOptions` does the actual parsing and populates every `cl::opt` global from `argv`. An out-of-range `-O` returns early with an error; `main()` propagates that return value straight out as the process exit code.

With `OptLevel` in scope, I go back and gate the three passes I added in [Building the Function Pipeline](#building-the-function-pipeline) behind it:

```cppdiff
*static void InitializeModuleAndManagers() {
*  ...
+  if (OptLevel != 0) {
*    FunctionPasses->addPass(InstCombinePass());
*    FunctionPasses->addPass(ReassociatePass());
*    FunctionPasses->addPass(GVNPass());
+  }
*}
```

Here's `main()` in full. I already covered the `InitializeNativeTarget*` calls in [Creating the ORC JIT](#creating-the-orc-jit), so I gray those out below. The new piece is the `ProcessCommandLine()` call at the very top, which has to run before anything else since it configures `Input` and `IsRepl`. Two more additions: this is a second call site for `PrintReplPrompt()`, which I explained in [Calling Functions I Didn't Write](#calling-functions-i-didnt-write) (this one replaces Chapter 7's raw `fprintf(stderr, "ready> ")` in `main()` itself, priming the very first prompt); and `JIT = ExitOnErr(PyxcJIT::Create())` is the JIT instantiation I introduced in [Creating the ORC JIT](#creating-the-orc-jit) but hadn't shown in its actual calling context until now. And I remove one thing: Chapter 7's `TheModule->print(errs(), nullptr);` at the end of `main()`. Every function now prints its own IR the moment I compile it, and by the time `MainLoop()` returns, `TheModule` only holds whatever fresh, empty module `InitializeModuleAndManagers()` created after the last transfer, not the code I actually just ran, so dumping it here would either print nothing useful or, if I forgot to reinitialize somewhere, reprint a function I already printed once:

```cppdiff
*int main(int argc, const char **argv) {
+  int commandLineResult = ProcessCommandLine(argc, argv);
+  if (commandLineResult != 0) {
+    return commandLineResult;
+  }
+
*  // Initialise LLVM's backend for the host machine. These three calls
*  // together register the native target's instruction set, assembler, and
*  // disassembler so the JIT can compile and link for the current CPU.
*  InitializeNativeTarget();
*  InitializeNativeTargetAsmPrinter();
*
+  // Prime the REPL: print the first prompt and load the first token.
+  // Every parse function expects CurrentToken to be loaded before it is called.
+  PrintReplPrompt();
+  getNextToken();
+
+  // Create the JIT first — InitializeModuleAndManagers() needs JIT in
+  // order to set the data layout on the new module.
+  JIT = ExitOnErr(PyxcJIT::Create());
*  InitializeModuleAndManagers();
*
*  MainLoop();
*
-  // Print out all of the generated code.
-  TheModule->print(errs(), nullptr);
-
*  return 0;
*}
```

## Build and Run

```bash
cd code/chapter-08
cmake -S . -B build && cmake --build build
./build/pyxc
```

```bash
llvm-lit -v test/
```

## Try It

### `extern` Resolves from the Process

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

### The Pythagorean Identity

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

### The Optimizer at Work

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

### The Runtime Library

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
