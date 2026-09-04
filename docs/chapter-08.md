---
section: "LLVM and Execution"
description: "Execute pyxc with ORC JIT, optimize functions, and resolve extern symbols."
---

# 8. pyxc: JIT and Optimization

Next: execute the LLVM IR.

Chapter 7 stops at:

```text
AST -> LLVM IR
```

Add the runtime boundary:

```text
LLVM module -> ORC JIT -> callable machine code -> double result
```

Also add a small optimization pipeline and `extern def` so generated code can call functions outside pyxc.

Work in:

```bash
cd code/chapter-08
```

## 1. Link the JIT and Pass Libraries

Update the LLVM component list in `CMakeLists.txt`:

```cmake
llvm_map_components_to_libnames(LLVM_LIBS
  OrcJIT
  Passes
  nativecodegen
)

target_link_libraries(pyxc PRIVATE ${LLVM_LIBS})
```

If the LLVM installation was built without RTTI, match that setting:

```cmake
if(NOT LLVM_ENABLE_RTTI)
  target_compile_options(pyxc PRIVATE -fno-rtti)
endif()
```

Include the repository's ORC wrapper and pass headers:

```cpp
#include "../include/PyxcJIT.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Scalar/Reassociate.h"
```

## 2. Create the JIT Before the Module

Add:

```cpp
static unique_ptr<PyxcJIT> JIT;
static ExitOnError ExitOnErr;
```

Initialize LLVM's native target in `main()`:

```cpp
InitializeNativeTarget();
InitializeNativeTargetAsmPrinter();
```

Then create the JIT before creating the module:

```cpp
JIT = ExitOnErr(PyxcJIT::Create());
InitializeModuleAndManagers();
```

The order matters because the module needs the JIT's target data layout.

## 3. Give Every Module the Target Layout

In `InitializeModuleAndManagers()`, after constructing `TheModule`, add:

```cpp
TheModule->setDataLayout(JIT->getDataLayout());
```

The data layout describes host details such as pointer sizes and alignment. JIT-generated IR must agree with the machine that will execute it.

## 4. Add the Optimization Managers

Add the globals:

```cpp
static unique_ptr<FunctionPassManager> FunctionPasses;
static unique_ptr<LoopAnalysisManager> LoopAnalyses;
static unique_ptr<FunctionAnalysisManager> FunctionAnalyses;
static unique_ptr<CGSCCAnalysisManager> CallGraphAnalyses;
static unique_ptr<ModuleAnalysisManager> ModuleAnalyses;
```

Construct them in `InitializeModuleAndManagers()`:

```cpp
FunctionPasses = make_unique<FunctionPassManager>();
LoopAnalyses = make_unique<LoopAnalysisManager>();
FunctionAnalyses = make_unique<FunctionAnalysisManager>();
CallGraphAnalyses = make_unique<CGSCCAnalysisManager>();
ModuleAnalyses = make_unique<ModuleAnalysisManager>();
```

Add a compact function pipeline:

```cpp
if (OptLevel != 0) {
  FunctionPasses->addPass(InstCombinePass());
  FunctionPasses->addPass(ReassociatePass());
  FunctionPasses->addPass(GVNPass());
}
```

Register analyses and connect their proxies:

```cpp
PassBuilder PB;
PB.registerModuleAnalyses(*ModuleAnalyses);
PB.registerCGSCCAnalyses(*CallGraphAnalyses);
PB.registerFunctionAnalyses(*FunctionAnalyses);
PB.registerLoopAnalyses(*LoopAnalyses);
PB.crossRegisterProxies(*LoopAnalyses, *FunctionAnalyses,
                        *CallGraphAnalyses, *ModuleAnalyses);
```

Run the pipeline after function verification:

```cpp
FunctionPasses->run(*TheFunction, *FunctionAnalyses);
```

## 5. Add `-O` Command-Line Parsing

Use LLVM's command-line library:

```cpp
static cl::OptionCategory PyxcCategory("Pyxc options");

static cl::opt<unsigned> OptLevel(
    "O", cl::desc("Optimization level"),
    cl::value_desc("0|1|2|3"), cl::Prefix,
    cl::init(2), cl::cat(PyxcCategory));
```

Parse it before initializing the JIT, and reject values above 3:

```cpp
cl::HideUnrelatedOptions(PyxcCategory);
cl::ParseCommandLineOptions(argc, argv, "pyxc\n");

if (OptLevel > 3) {
  fprintf(stderr, "Error: -O level must be 0, 1, 2, or 3\n");
  return -1;
}
```

For now, `-O0` disables the chapter's pipeline and `-O1` through `-O3` enable the same small pipeline. Later chapters can differentiate them.

## 6. Use One Module per JIT Submission

When a module is handed to ORC, the JIT takes ownership of its context and IR. Do not keep emitting into it.

After compiling a named function:

```cpp
ExitOnErr(JIT->addModule(
    ThreadSafeModule(std::move(TheModule),
                     std::move(TheContext))));

InitializeModuleAndManagers();
```

This gives the REPL a repeating lifecycle:

```text
create module -> emit one unit -> transfer module -> create fresh module
```

The compiled symbol remains in the JIT even though frontend ownership moved away.

## 7. Preserve Function Signatures Across Modules

A fresh module cannot see declarations from the previous module. Add a persistent registry:

```cpp
static map<string, unique_ptr<FunctionSignatureNode>>
    FunctionSignatures;
```

Replace direct module lookup in call codegen with:

```cpp
Function *getFunction(const string &Name) {
  if (auto *F = TheModule->getFunction(Name))
    return F;

  auto It = FunctionSignatures.find(Name);
  if (It != FunctionSignatures.end())
    return It->second->codegen();

  return nullptr;
}
```

Store a definition's signature before moving its module to the JIT. Later calls re-emit a declaration into the current module; ORC links that declaration to the previously compiled symbol.

The distinction is:

```text
signature registry -> frontend knowledge
JIT symbol table   -> compiled implementation
```

## 8. Add `extern def`

Add `tok_extern` and map the keyword:

```cpp
{"extern", tok_extern}
```

Extend the grammar:

```ebnf
top     = function-definition | external | top-level-expression ;
external = "extern" "def" function-signature ;
```

Add:

```cpp
static unique_ptr<FunctionSignatureNode> ParseExtern() {
  getNextToken(); // eat 'extern'

  if (CurrentToken != tok_def)
    return LogErrorSignature("Expected 'def' after 'extern'");
  getNextToken(); // eat 'def'

  return ParseFunctionSignature();
}
```

In `HandleExtern()`:

1. Parse the signature.
2. Reject a conflicting arity for an existing name.
3. Emit the LLVM declaration.
4. Store the AST signature in `FunctionSignatures`.

Add `tok_extern` dispatch to `MainLoop()`.

Now this is valid:

```pyxc
extern def sin(x)
```

The declaration tells LLVM the call shape. ORC resolves the implementation from the current process or linked libraries.

## 9. Execute a Top-Level Expression

Keep wrapping each expression in `__anon_expr`. After codegen, create a resource tracker:

```cpp
auto RT = JIT->getMainJITDylib().createResourceTracker();
```

Transfer the module under that tracker:

```cpp
auto TSM = ThreadSafeModule(std::move(TheModule),
                            std::move(TheContext));
ExitOnErr(JIT->addModule(std::move(TSM), RT));
InitializeModuleAndManagers();
```

Look up and call the generated function:

```cpp
auto ExprSymbol =
    ExitOnErr(JIT->lookup(AnonymousExpressionFunctionName));

double (*FP)() = ExprSymbol.toPtr<double (*)()>();
double Result = FP();
fprintf(stdout, "Evaluated to %f\n", Result);
```

Then release only this anonymous expression's code:

```cpp
ExitOnErr(RT->remove());
```

Named functions remain installed. Temporary top-level expressions do not accumulate indefinitely.

## 10. Add the Tiny Runtime Library

Export two C-linkage functions from the pyxc executable:

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

C linkage prevents C++ name mangling. The Windows export attribute makes the symbols visible to the JIT there.

Use them through ordinary declarations:

```pyxc
extern def printd(x)
extern def putchard(x)
```

## 11. Build and Run

```bash
cmake -S . -B build \
  -DLLVM_DIR="$(llvm-config --cmakedir)"
cmake --build build
./build/pyxc
```

Try direct execution:

```pyxc
ready> 1 + 2 * 3
```

Expected:

```text
Parsed a top-level expression.
Evaluated to 7.000000
```

Try a persistent definition:

```pyxc
ready> def square(x): x * x
ready> square(5)
```

Expected final result:

```text
Evaluated to 25.000000
```

Try the runtime:

```pyxc
ready> extern def printd(x)
ready> printd(42)
```

Expected:

```text
42.000000
Evaluated to 0.000000
```

Compare optimized and unoptimized IR:

```bash
./build/pyxc -O0
./build/pyxc -O2
```

Run the suite:

```bash
llvm-lit -v test/
```

What you built is the complete interactive execution loop:

```text
parse -> codegen -> optimize -> JIT -> lookup -> call -> release temporary code
```

Next: [Chapter 9](chapter-09.md) feeds the same compiler from a source file and makes IR output optional.

## Need Help?

Build issues? Questions?

- [Report a problem with GitHub Issues](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- [Ask a question in GitHub Discussions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:

- Your operating system and version
- The chapter number
- The exact command you ran
- The complete error message
- The output of `c++ --version` and `cmake --version`
- The output of `llvm-config --version` for Chapter 6 and later
