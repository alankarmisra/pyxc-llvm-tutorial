# Chapter 7: Optimization and JIT Execution

## Introduction

In previous chapters, we built a parser and code generator for Pyxc. We can parse code and generate LLVM IR, but we can't actually *run* anything yet. This chapter adds two crucial capabilities:

1. **Optimization** - Making the generated code faster using LLVM's optimization passes
2. **JIT Compilation** - Executing code immediately in the REPL

By the end of this chapter, you'll be able to type expressions and function definitions into the REPL and see them execute instantly.

## Constant Folding - The Free Lunch

Let's start the REPL and try a simple example:

```bash
$ ./build/pyxc repl -l
ready> def test(x): return 1+2+x
```

With the `-l` flag, we can see the generated IR:

```llvm
define double @test(double %x) {
entry:
  %addtmp = fadd double 3.000000e+00, %x
  ret double %addtmp
}
```

Wait a minute! We wrote `1+2+x` but the IR shows `3.0+x`. Where did the calculation of `1+2` go?

This is **constant folding** - a basic optimization that LLVM's `IRBuilder` does automatically. When you call `Builder->CreateFAdd(...)` with two constants, it doesn't create an instruction. Instead, it computes the result at compile time and returns a constant.

Without constant folding, we'd see:

```llvm
define double @test(double %x) {
entry:
  %addtmp = fadd double 2.000000e+00, 1.000000e+00
  %addtmp1 = fadd double %addtmp, %x
  ret double %addtmp1
}
```

The IRBuilder's constant folding is "free" - we didn't have to do anything special to get it. This is one reason to always use `IRBuilder` instead of manually creating LLVM instructions.

## The Limits of Constant Folding

Now try a slightly more complex example:

```bash
ready> def test(x): return (1+2+x)*(x+(1+2))
```

The generated IR:

```llvm
define double @test(double %x) {
entry:
  %addtmp = fadd double 3.000000e+00, %x
  %addtmp1 = fadd double %x, 3.000000e+00
  %multmp = fmul double %addtmp, %addtmp1
  ret double %multmp
}
```

The constants folded (`1+2` became `3`), but we're computing `x+3` **twice**! The code should be:

```
tmp = x + 3
result = tmp * tmp
```

Why doesn't IRBuilder fix this? Because IRBuilder only does **local** optimizations - it looks at one instruction at a time as you build IR. It can't see that `%addtmp` and `%addtmp1` compute the same value.

To fix this, we need **global** optimizations that analyze the whole function:
- **Reassociation**: Reorder expressions to make common subexpressions obvious
- **Common Subexpression Elimination (CSE)**: Remove redundant calculations

This is where LLVM's optimization passes come in.

## LLVM Optimization Passes

LLVM provides hundreds of optimization passes. Each pass does a specific transformation:

- `InstCombinePass`: Combines instructions using algebraic simplification
- `ReassociatePass`: Reorders associative expressions (a+b+c+d)
- `GVNPass`: Global Value Numbering - eliminates redundant computations
- `SimplifyCFGPass`: Simplifies control flow (removes dead blocks, merges blocks, etc.)

Passes are organized into two categories:

**Function Passes**: Operate on one function at a time (what we'll use in the REPL)
**Module Passes**: Operate on the entire module (for whole-program optimization)

For the REPL, we want function passes - optimize each function as it's typed in.

## Setting Up the Function Pass Manager

We need infrastructure to run optimization passes. Add these globals after the other code generation globals:

```cpp
static std::unique_ptr<LLVMContext> TheContext;
static std::unique_ptr<Module> TheModule;
static std::unique_ptr<IRBuilder<>> Builder;
static std::map<std::string, Value *> NamedValues;
static bool ShouldEmitLLVM = false;
static std::unique_ptr<PyxcJIT> TheJIT;

// Optimization infrastructure
static std::unique_ptr<FunctionPassManager> TheFPM;
static std::unique_ptr<LoopAnalysisManager> TheLAM;
static std::unique_ptr<FunctionAnalysisManager> TheFAM;
static std::unique_ptr<CGSCCAnalysisManager> TheCGAM;
static std::unique_ptr<ModuleAnalysisManager> TheMAM;
static std::unique_ptr<PassInstrumentationCallbacks> ThePIC;
static std::unique_ptr<StandardInstrumentations> TheSI;
static ExitOnError ExitOnErr;
```

**What are these?**

- `TheFPM`: The **FunctionPassManager** - runs optimization passes on functions
- `TheLAM/TheFAM/TheCGAM/TheMAM`: **Analysis managers** for different IR levels (loops, functions, call graphs, modules)
- `ThePIC/TheSI`: Instrumentation for pass debugging and profiling
- `ExitOnErr`: Helper for error handling with the JIT

## Initializing Optimization Passes

Create (or update) the `InitializeModuleAndManagers()` function:

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

  // Add transform passes.
  TheFPM->addPass(InstCombinePass());
  TheFPM->addPass(ReassociatePass());
  TheFPM->addPass(GVNPass());
  TheFPM->addPass(SimplifyCFGPass());

  // Register analysis passes.
  PassBuilder PB;
  PB.registerModuleAnalyses(*TheMAM);
  PB.registerFunctionAnalyses(*TheFAM);
  PB.crossRegisterProxies(*TheLAM, *TheFAM, *TheCGAM, *TheMAM);
}
```

**The four optimization passes:**

1. **InstCombinePass**: "Instruction combining" - uses algebraic identities to simplify instructions
   - Example: `x * 1` → `x`, `x + 0` → `x`

2. **ReassociatePass**: Reorders associative expressions to expose common subexpressions
   - Example: `(a+b) + (c+b)` → `(a+c) + (b+b)` → `(a+c) + 2*b`

3. **GVNPass**: "Global Value Numbering" - eliminates redundant computations
   - Example: If `x+3` is computed twice, compute once and reuse

4. **SimplifyCFGPass**: Simplifies control flow graph
   - Removes unreachable blocks, merges blocks, etc.

This is a standard "cleanup" optimization pipeline that works well for most code.

## Running the Optimizer

Now we need to actually *run* these passes after generating a function. Update `FunctionAST::codegen()`:

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

    // Validate the generated code
    verifyFunction(*TheFunction);

    // Run the optimizer on the function.
    TheFPM->run(*TheFunction, *TheFAM);

    return TheFunction;
  }

  TheFunction->eraseFromParent();
  return nullptr;
}
```

The only change is adding `TheFPM->run(*TheFunction, *TheFAM)` after `verifyFunction()`.

## Testing the Optimizer

Now let's try that earlier example again:

```bash
ready> def test(x): return (1+2+x)*(x+(1+2))
```

Generated IR:

```llvm
define double @test(double %x) {
entry:
  %addtmp = fadd double %x, 3.000000e+00
  %multmp = fmul double %addtmp, %addtmp
  ret double %multmp
}
```

Perfect! The optimizer:
1. Folded `1+2` to `3` (constant folding)
2. Recognized that `3+x` and `x+3` are the same (reassociation)
3. Eliminated the duplicate computation (GVN)
4. Produced `tmp = x+3; result = tmp*tmp`

## Adding JIT Compilation

Generating optimized IR is nice, but we still can't *run* anything. Let's add JIT (Just-In-Time) compilation so we can execute code immediately.

### What is JIT Compilation?

JIT compilation converts LLVM IR to native machine code **at runtime** and executes it immediately. This is how languages like JavaScript, Java, and C# run fast despite being "interpreted."

For Pyxc, this means:
- Type a function definition → Compile to machine code → Store in memory
- Type an expression → Compile to machine code → Execute → Print result

### Initialize the JIT

Add a global for the JIT (already shown above):

```cpp
static std::unique_ptr<PyxcJIT> TheJIT;
```

In `main()`, initialize it before creating the first module:

```cpp
int main(int argc, const char **argv) {
  cl::ParseCommandLineOptions(argc, argv, "pyxc chapter07\n");

  DiagSourceMgr.reset();

  if (ReplCommand && ReplEmitTokens) {
    EmitTokenStream();
    return 0;
  }

  // REPL setup
  HadError = false;
  InteractiveMode = true;
  if (InteractiveMode)
    fprintf(stderr, "ready> ");
  getNextToken();

  // Initialize JIT
  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();
  InitializeNativeTargetAsmParser();
  TheJIT = ExitOnErr(PyxcJIT::Create());

  // Make the module, which holds all the code and optimization managers.
  InitializeModuleAndManagers();
  ShouldEmitLLVM = ReplEmitLLVM;

  // Run the main "interpreter loop" now.
  MainLoop();

  return 0;
}
```

The `PyxcJIT` class is a simple JIT implementation in `include/PyxcJIT.h`. We'll explore its internals in later chapters.

### Update InitializeModuleAndManagers

The JIT needs to know the data layout of the target machine. Update the initialization:

```cpp
static void InitializeModuleAndManagers() {
  TheContext = std::make_unique<LLVMContext>();
  TheModule = std::make_unique<Module>("PyxcJIT", *TheContext);

  if (TheJIT)
    TheModule->setDataLayout(TheJIT->getDataLayout());  // <-- Add this

  Builder = std::make_unique<IRBuilder<>>(*TheContext);
  // ... rest of initialization ...
}
```

## Executing Functions

### Function Definitions

When a function is defined, we need to add it to the JIT. Update `HandleDefinition()`:

```cpp
static void HandleDefinition() {
  if (auto FnAST = ParseDefinition()) {
    if (auto *FnIR = FnAST->codegen()) {
      if (ShouldEmitLLVM) {
        fprintf(stderr, "Read function definition:\n");
        FnIR->print(errs());
        fprintf(stderr, "\n");
      }

      // Add the module to the JIT
      ExitOnErr(TheJIT->addModule(
          ThreadSafeModule(std::move(TheModule), std::move(TheContext))));
      InitializeModuleAndManagers();
    }
  } else {
    SynchronizeToLineBoundary();
  }
}
```

**What's happening:**

1. Generate the function IR
2. Add the entire module to the JIT (compiles all functions in it to machine code)
3. Create a fresh module for new code

Each function gets its own module. This allows us to manage memory efficiently.

### Top-Level Expressions

For top-level expressions (like `4+5`), we want to:
1. Wrap the expression in an anonymous function
2. Compile it
3. Execute it
4. Print the result
5. Delete it

Update `HandleTopLevelExpression()`:

```cpp
static void HandleTopLevelExpression() {
  if (auto FnAST = ParseTopLevelExpr()) {
    if (auto *FnIR = FnAST->codegen()) {
      // Create a ResourceTracker to manage memory for this expression
      auto RT = TheJIT->getMainJITDylib().createResourceTracker();

      // Add the module to the JIT
      auto TSM = ThreadSafeModule(std::move(TheModule), std::move(TheContext));
      ExitOnErr(TheJIT->addModule(std::move(TSM), RT));
      InitializeModuleAndManagers();

      if (ShouldEmitLLVM) {
        FnIR->print(errs());
        fprintf(stderr, "\n");
      }

      // Look up the JIT-compiled function
      auto ExprSymbol = ExitOnErr(TheJIT->lookup("__anon_expr"));

      // Get a function pointer to the compiled code
      double (*FP)() = ExprSymbol.toPtr<double (*)()>();

      // Call it!
      fprintf(stderr, "Evaluated to %f\n", FP());

      // Delete the anonymous expression module from the JIT
      ExitOnErr(RT->remove());
    }
  } else {
    SynchronizeToLineBoundary();
  }
}
```

**Key steps:**

1. **ResourceTracker**: Tracks memory for this specific module so we can free it later
2. **addModule**: Compiles the IR to machine code
3. **lookup**: Finds the function by name (`__anon_expr`)
4. **toPtr**: Gets a C function pointer to the compiled code
5. **Call it**: Just call the function pointer like any C function!
6. **remove**: Free the memory for this expression

## Cross-Module Function Calls

There's a problem. Try this:

```bash
ready> def add(x, y): return x + y

ready> add(3.0, 4.0)
Evaluated to 7.000000

ready> add(5.0, 6.0)
Error: Unknown function referenced
```

What happened? Each module goes into the JIT separately. When we executed `add(3.0, 4.0)`, it was in an anonymous expression module. After we removed that module, `add` disappeared!

**Solution**: Keep a map of function prototypes and regenerate declarations in each new module.

### Add a Prototype Map

Add a global map (we've already done this in the parser):

```cpp
static std::map<std::string, std::unique_ptr<PrototypeAST>> FunctionProtos;
```

### Create a getFunction Helper

Add this helper to find or regenerate function declarations:

```cpp
static Function *getFunction(const std::string &Name) {
  // First, see if the function has already been added to the current module.
  if (auto *F = TheModule->getFunction(Name))
    return F;

  // If not, check whether we can codegen the declaration from some existing
  // prototype.
  auto FI = FunctionProtos.find(Name);
  if (FI != FunctionProtos.end())
    return FI->second->codegen();

  // If no existing prototype exists, return null.
  return nullptr;
}
```

This function:
1. Checks if the function exists in the current module
2. If not, looks for a prototype and generates a declaration
3. Returns null if the function doesn't exist anywhere

### Update FunctionAST::codegen()

Store the prototype before generating the function:

```cpp
Function *FunctionAST::codegen() {
  // Transfer ownership of the prototype to FunctionProtos, but keep a reference
  auto &P = *Proto;
  FunctionProtos[Proto->getName()] = std::move(Proto);
  Function *TheFunction = getFunction(P.getName());  // <-- Use getFunction

  if (!TheFunction)
    return nullptr;

  // ... rest of codegen ...
}
```

### Update CallExprAST::codegen()

Use `getFunction` instead of `TheModule->getFunction`:

```cpp
Value *CallExprAST::codegen() {
  Function *CalleeF = getFunction(Callee);  // <-- Use getFunction
  if (!CalleeF)
    return LogError<Value *>("Unknown function referenced");

  // ... rest of codegen ...
}
```

### Update HandleExtern

Store extern declarations in the prototype map:

```cpp
static void HandleExtern() {
  if (auto ProtoAST = ParseExtern()) {
    if (auto *FnIR = ProtoAST->codegen()) {
      if (ShouldEmitLLVM) {
        fprintf(stderr, "Read extern:\n");
        FnIR->print(errs());
        fprintf(stderr, "\n");
      }
      FunctionProtos[ProtoAST->getName()] = std::move(ProtoAST);
    }
  } else {
    getNextToken();
  }
}
```

### Preventing Duplicate Definitions

Since we keep prototypes around, we need to prevent redefining functions. Update `ParsePrototype()`:

```cpp
static std::unique_ptr<PrototypeAST> ParsePrototype() {
  if (CurTok != tok_identifier)
    return LogError<ProtoPtr>("Expected function name in prototype");

  std::string FnName = IdentifierStr;
  getNextToken();

  // Reject duplicate function definitions
  auto FI = FunctionProtos.find(FnName);
  if (FI != FunctionProtos.end())
    return LogError<ProtoPtr>(("Duplicate definition for " + FnName).c_str());

  // ... rest of parsing ...
}
```

## Testing It All

Now everything should work:

```bash
ready> def add(x, y): return x + y
Read function definition:
define double @add(double %x, double %y) {
entry:
  %addtmp = fadd double %x, %y
  ret double %addtmp
}

ready> add(3.0, 4.0)
Evaluated to 7.000000

ready> add(5.0, 6.0)
Evaluated to 11.000000
```

Perfect! The function persists across calls.

## Calling External Functions

The JIT has a neat feature: it can call functions from the host process. Try this:

```bash
ready> extern def sin(x)
Read extern:
declare double @sin(double)

ready> sin(1.0)
Evaluated to 0.841471

ready> extern def cos(x)
Read extern:
declare double @cos(double)

ready> def ident(x): return sin(x)*sin(x) + cos(x)*cos(x)
Read function definition:
define double @ident(double %x) {
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

ready> ident(4.0)
Evaluated to 1.000000
```

How does this work? The JIT's symbol resolution:

1. First, search modules in the JIT (newest to oldest)
2. If not found, call `dlsym()` on the process itself
3. Since `sin` and `cos` are in `libm` (loaded by the process), the JIT finds them

This means we can extend Pyxc by writing C++ functions!

## Adding Custom Runtime Functions

We can add our own functions to the runtime. Add this to `pyxc.cpp`:

```cpp
#ifdef _WIN32
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

/// putchard - putchar that takes a double and returns 0.
extern "C" DLLEXPORT double putchard(double X) {
  fputc((char)X, stderr);
  return 0;
}
```

**Why `extern "C"`?** Prevents C++ name mangling so the JIT can find it.

**Why `DLLEXPORT`?** On Windows, we need to explicitly export symbols.

Now we can use it:

```bash
ready> extern def putchard(x)
ready> putchard(72)    # ASCII for 'H'
H
Evaluated to 0.000000

ready> putchard(101)   # 'e'
e
Evaluated to 0.000000
```

You can use this to add I/O, file operations, or any other functionality to Pyxc.

## Compile and Test

Build the chapter:

```bash
cd code/chapter07
./build.sh
```

Or manually:

```bash
cmake -S . -B build
cmake --build build
```

Run the REPL:

```bash
$ ./build/pyxc repl
ready> def add(x, y): return x + y
ready> add(3.0, 4.0)
Evaluated to 7.000000
```

Run tests:

```bash
$ lit test/
Testing Time: 0.52s
  Total Discovered Tests: 45
  Passed: 45 (100.00%)
```

## What We Accomplished

This chapter added major functionality:

1. **Optimization passes** that make generated code faster
   - Instruction combining, reassociation, CSE, CFG simplification
   - Runs automatically on every function

2. **JIT compilation** so code executes immediately
   - Functions are compiled to native machine code
   - Top-level expressions evaluate and print results
   - Cross-module calls work via prototype tracking

3. **External function calls** via symbol resolution
   - Can call standard library functions (sin, cos, etc.)
   - Can add custom runtime functions

## What's Next

We can now:
- Parse Pyxc code
- Generate LLVM IR
- Optimize it
- JIT compile it
- Execute it

But our language is still very limited - just arithmetic and function calls.

**Chapter 8** will add control flow (`if/else`), allowing us to write real programs with conditional logic.

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
